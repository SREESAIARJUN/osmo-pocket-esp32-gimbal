# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC (Cycle 14)

The script runs two independent checks and cross-references them:

1. **Geometric engine.** It parses the `.kicad_pcb` file directly and builds copper geometry with Shapely. It then checks:
   - Netlist continuity: every pad on a net must be galvanically connected.
   - Minimum track width ≥ 0.1 mm.
   - Minimum copper-to-copper clearance ≥ 0.1 mm between different nets on the same layer.
   - Whether the board's design rules in `.kicad_pro` are set to at least 0.1 mm.
2. **KiCad's own DRC.** It runs `kicad-cli pcb drc` (KiCad 8) with schematic parity and parses the JSON report.

The script exits with a non-zero code if any check fails, so it can gate CI.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle14.py
=====================
KiCad 8 PCB verification - Cycle 14
  * Netlist continuity (all pads of each net galvanically connected)
  * Minimum trace width   >= 0.100 mm
  * Minimum copper space  >= 0.100 mm (different nets, same copper layer)
  * Project design-rule sanity (.kicad_pro)
  * Cross-check with KiCad's native DRC via kicad-cli (optional)

Requirements:
    pip install "shapely>=2.0"
    KiCad 8 kicad-cli on PATH (optional, use --no-kicad-cli to skip)

Usage:
    python verify_pcb_cycle14.py board.kicad_pcb [--report report.json]
                                 [--min-width 0.1] [--min-clearance 0.1]
                                 [--no-kicad-cli] [--kicad-cli PATH]

Exit codes:
    0 = PASS, 1 = verification FAIL, 2 = tool/parse error
"""
from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

try:
    from shapely import affinity
    from shapely.geometry import LineString, Point, Polygon, box
    from shapely.strtree import STRtree
except ImportError:
    sys.exit("ERROR: shapely>=2.0 is required (pip install shapely)")

CYCLE = "Cycle 14"
TOL = 1e-4          # mm, numerical tolerance for clearance comparisons
CONNECT_TOL = 1e-4  # mm, items closer than this are considered touching
ARC_SEGMENTS = 32


# --------------------------------------------------------------------------
# S-expression parser
# --------------------------------------------------------------------------
def tokenize(text: str):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c in "()":
            yield c
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 1
                buf.append(text[j])
                j += 1
            yield ("STR", "".join(buf))
            i = j + 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()"':
                j += 1
            yield text[i:j]
            i = j


def parse_sexpr(text: str):
    stack, cur = [], []
    for tok in tokenize(text):
        if tok == "(":
            stack.append(cur)
            cur = []
        elif tok == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
        else:
            cur.append(tok[1] if isinstance(tok, tuple) else tok)
    if len(cur) != 1:
        raise ValueError("Malformed S-expression")
    return cur[0]


def children(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


def child(node, name):
    for c in node:
        if isinstance(c, list) and c and c[0] == name:
            return c
    return None


def fval(node, name, idx=1, default=None):
    c = child(node, name)
    if c is None or len(c) <= idx:
        return default
    try:
        return float(c[idx])
    except (TypeError, ValueError):
        return default


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------
@dataclass
class CopperItem:
    uid: int
    kind: str                 # track, arc, via, pad, zone
    net: str                  # net name ("" = unconnected)
    layers: list[str]
    geom: object              # shapely geometry (copper outline)
    ref: str = ""             # human-readable reference
    width: float | None = None
    per_layer_geom: dict = field(default_factory=dict)  # zones: layer -> geom

    def geom_on(self, layer):
        return self.per_layer_geom.get(lay