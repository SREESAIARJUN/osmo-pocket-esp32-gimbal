# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC

The script `verify_pcb.py` runs two independent verification passes:

1. **Native geometric analysis.** It parses the `.kicad_pcb` file directly and uses Shapely to check three things:
   - Minimum trace width of 0.1 mm or more.
   - Copper-to-copper clearance of 0.1 mm or more between different nets on the same layer. This covers tracks, arcs, vias, pads and filled zones.
   - Net continuity. Every pad of each net must form a single connected island across tracks, vias, zones and through-hole pads.
2. **KiCad's own DRC engine (optional).** It runs `kicad-cli pcb drc` with schematic parity and merges the JSON results into the report.

It also checks the `.kicad_pro` design rules, so the project itself is confirmed to be configured for 0.1/0.1 mm.

```python
#!/usr/bin/env python3
"""
verify_pcb.py - KiCad 8 PCB verification: netlist continuity + 0.1 mm trace/space DRC.
Verification cycle: 28

Usage:
    python verify_pcb.py board.kicad_pcb [--min-width 0.1] [--min-clearance 0.1]
                         [--kicad-cli] [--report report.json]

Exit codes:
    0 = PASS, 1 = FAIL (violations found), 2 = usage / parse error

Requires: Python 3.9+, shapely >= 2.0  (pip install shapely)
Optional: kicad-cli (KiCad 8) on PATH for --kicad-cli cross-check.
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
    from shapely.geometry import LineString, Point, Polygon, box
    from shapely.affinity import rotate
    from shapely.strtree import STRtree
except ImportError:
    sys.exit("ERROR: shapely >= 2.0 required (pip install shapely)")

CYCLE = 28
EPS = 1e-4          # mm - numeric tolerance (0.1 um)
CONNECT_TOL = 1e-4  # mm - items closer than this are considered touching

# --------------------------------------------------------------------------
# S-expression parser
# --------------------------------------------------------------------------
_TOKEN_RE = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+')


def parse_sexpr(text: str):
    stack, cur = [], []
    for m in _TOKEN_RE.finditer(text):
        tok = m.group(0)
        if tok == "(":
            stack.append(cur)
            cur = []
        elif tok == ")":
            if not stack:
                raise ValueError("Unbalanced ')' in S-expression")
            done, cur = cur, stack.pop()
            cur.append(done)
        elif tok.startswith('"'):
            cur.append(bytes(tok[1:-1], "utf-8").decode("unicode_escape"))
        else:
            cur.append(tok)
    if stack:
        raise ValueError("Unbalanced '(' in S-expression")
    return cur[0] if len(cur) == 1 else cur


def children(node, name):
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


def child(node, name):
    for c in node:
        if isinstance(c, list) and c and c[0] == name:
            return c
    return None


def fnum(v) -> float:
    return float(v)


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------
@dataclass
class CopperItem:
    idx: int
    kind: str               # track | arc | via | pad | zone
    net: int
    layers: frozenset
    geom: object            # shapely geometry (copper outline)
    ref: str                # human-readable descriptor
    width: float | None = None


@dataclass
class Board:
    nets: dict = field(default_factory=dict)       # id -> name
    copper_layers: list = field(default_factory=list)  # ordered
    items: list = field(default_factory=list)


# --------------------------------------------------------------------------
# Geometry helpers
# --------------------------------------------------------------------------
def xy(node):
    return fnum(node[1]), fnum(node[2])


def rot_pt(x, y, deg):
    """Rotate in KiCad convention (Y down, positive angle = CCW on screen)."""
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return x * c + y * s, -x * s + y * c


def arc_points(p0, pm, p1, seg_len=0.02):
    """Approximate a 3-point arc with a polyline."""
    (x1, y