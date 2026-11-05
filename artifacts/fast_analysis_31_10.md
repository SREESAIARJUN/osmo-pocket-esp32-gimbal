# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space (Cycle 31)

This script reads a `.kicad_pcb` file directly and runs these checks:

1. **Trace width**: every track segment and arc must be at least 0.1 mm wide.
2. **Copper clearance**: copper items on different nets must be at least 0.1 mm apart on the same layer. This covers tracks, arcs, vias, pads and filled zones.
3. **Net continuity**: all pads on each net must be joined by copper (tracks, vias and filled zones). Disconnected pads are reported as broken nets.
4. **Schematic parity** (optional, requires `--sch`): PCB pad-to-net assignments are compared against a netlist exported from the schematic with `kicad-cli`.
5. **Native DRC cross-check** (optional, requires `--kicad-drc`): runs `kicad-cli pcb drc` and adds its errors to the report.

The script exits with code `0` on PASS and `1` on FAIL, so it can gate a CI pipeline.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle31.py - KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Requirements:
    pip install "shapely>=2.0"
    kicad-cli (KiCad 8) on PATH  -- only for --sch / --kicad-drc options

Usage:
    python verify_pcb_cycle31.py board.kicad_pcb
    python verify_pcb_cycle31.py board.kicad_pcb --sch board.kicad_sch --kicad-drc \
        --json report_cycle31.json

Exit code: 0 = PASS, 1 = FAIL, 2 = usage/parse error
"""

from __future__ import annotations

import argparse
import json
import math
import re
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

CYCLE = 31
MIN_TRACK_MM = 0.100
MIN_CLEARANCE_MM = 0.100
TOL = 1e-4          # numeric tolerance in mm
ARC_SEGMENTS = 16   # tessellation for arcs / round shapes


# ----------------------------------------------------------------------------
# S-expression parsing
# ----------------------------------------------------------------------------
_TOKEN_RE = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')


def parse_sexpr(text: str):
    stack, cur = [], []
    for tok in _TOKEN_RE.findall(text):
        if tok == "(":
            stack.append(cur)
            cur = []
        elif tok == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
        elif tok.startswith('"'):
            cur.append(bytes(tok[1:-1], "utf-8").decode("unicode_escape"))
        else:
            cur.append(tok)
    if stack:
        raise ValueError("Unbalanced parentheses in S-expression")
    return cur[0]


def find(node, key):
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def findall(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def fnum(node, key, idx=1, default=0.0):
    n = find(node, key)
    return float(n[idx]) if n is not None and len(n) > idx else default


# ----------------------------------------------------------------------------
# Data model
# ----------------------------------------------------------------------------
@dataclass
class CopperItem:
    uid: int
    kind: str                 # track | arc | via | pad | zone
    net: int
    layers: set
    geom: object              # shapely geometry (true copper outline)
    desc: str
    width: float | None = None
    pad_key: tuple | None = None   # (ref, pad number) for pads


@dataclass
class Report:
    width_violations: list = field(default_factory=list)
    clearance_violations: list = field(default_factory=list)
    open_nets: list = field(default_factory=list)
    parity_errors: list = field(default_factory=list)
    kicad_drc: dict | None = None

    @property
    def passed(self) -> bool:
        drc_ok = self.kicad_drc is None or self.kicad_drc.get("errors", 0) == 0
        return not (self.width_violations or self.clearance_violations
                    or self.open_nets or self.parity_errors) and drc_ok


# ----------------------------------------------------------------------------
# Geometry helpers
# ----------------------------------------------------------------------------
def arc_points(s, m, e, n=ARC_SEGMENTS):
    """Points along a 3-point arc (start, mid, end)."""
    (x1, y1), (x2, y2), (x3, y3) = s, m, e
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-12:
        return [s, e]  # collinear: