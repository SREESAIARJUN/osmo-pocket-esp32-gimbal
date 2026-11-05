# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Clearance (Cycle 1)

The script runs two independent checks and cross-checks them against each other:

1. **KiCad's own DRC.** It runs `kicad-cli pcb drc` with schematic parity enabled and parses the JSON report.
2. **A standalone geometric checker.** It parses `.kicad_pcb` directly and does the following:
   - Builds copper geometry for pads, tracks, arcs, vias and zone fills.
   - Checks netlist continuity using union-find over touching same-net copper. This finds open nets and islands.
   - Checks clearance of 0.1 mm or more between different-net copper on each layer. This also detects shorts.
   - Checks that track width is 0.1 mm or more.
   - Compares pad-to-net assignments against the schematic netlist.
3. **A project-rule audit.** It checks that the `.kicad_pro` design rules are not looser than 0.1 mm. If they are, KiCad's DRC would silently pass violations.

```python
#!/usr/bin/env python3
"""
kicad_verify.py — KiCad 8 PCB verification, Cycle 1
  * Netlist continuity (opens, islands, shorts, schematic-vs-PCB pad nets)
  * DRC clearance compliance: 0.1 mm min trace width / 0.1 mm min space

Usage:
  python kicad_verify.py board.kicad_pcb [--sch board.kicad_sch]
                         [--min-width 0.1] [--min-space 0.1]
                         [--report report.json] [--no-kicad-cli]

Requires: Python 3.9+, shapely>=2.0   (pip install shapely)
Optional: kicad-cli (KiCad 8) on PATH for native DRC + netlist export.
Exit code: 0 = PASS, 1 = FAIL, 2 = tool/usage error.
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
    sys.exit("ERROR: shapely>=2.0 required (pip install shapely)")

CYCLE = 1
EPS = 1e-4  # mm tolerance for float noise


# --------------------------------------------------------------------------
# S-expression parser
# --------------------------------------------------------------------------
def parse_sexpr(text: str):
    """Parse a KiCad S-expression into nested lists of strings."""
    stack, cur, i, n = [], [], 0, len(text)
    while i < n:
        c = text[i]
        if c == "(":
            stack.append(cur)
            cur = []
            i += 1
        elif c == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
            i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 1
                buf.append(text[j])
                j += 1
            cur.append("".join(buf))
            i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in "()":
                j += 1
            cur.append(text[i:j])
            i = j
    return cur[0] if cur else []


def find(node, key):
    """First child list whose head is `key`."""
    for ch in node:
        if isinstance(ch, list) and ch and ch[0] == key:
            return ch
    return None


def find_all(node, key):
    return [ch for ch in node if isinstance(ch, list) and ch and ch[0] == key]


def fval(node, key, idx=1, default=None, cast=float):
    ch = find(node, key)
    if ch is None or len(ch) <= idx:
        return default
    try:
        return cast(ch[idx])
    except (TypeError, ValueError):
        return default


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------
@dataclass
class CopperItem:
    idx: int
    kind: str              # pad | track | arc | via | zone
    net: int
    netname: str
    layers: frozenset
    geom: object           # shapely geometry (copper outline)
    desc: str
    width: float | None = None
    