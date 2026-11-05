# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Clearance (Cycle 15)

The script performs two independent verification passes:

1. **Native geometric analysis.** It parses the `.kicad_pcb` file directly and uses Shapely to check:
   - **Copper continuity:** every pad in a net must be connected through tracks, arcs, vias or zone fills.
   - **Shorts:** copper from different nets must not touch.
   - **Clearance:** copper-to-copper spacing must be at least 0.1 mm on each layer.
   - **Trace width:** every track must be at least 0.1 mm wide.
   - **Rule settings:** the `.kicad_pro` design rules and net classes must not allow less than 0.1 mm.
2. **KiCad's own DRC engine.** It runs `kicad-cli pcb drc` with the schematic-parity check enabled, then sorts the results into categories.

If any check fails, the script exits with a non-zero code, so it can be used as a CI gate.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle15.py — KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Usage:
    python verify_pcb_cycle15.py board.kicad_pcb [--min 0.1] [--report out.json]
                                 [--no-kicad-cli] [--skip-parity]

Requirements:
    Python 3.9+, shapely>=2.0 (pip install "shapely>=2.0")
    Optional: kicad-cli (KiCad 8) on PATH for the authoritative DRC pass.

Exit codes: 0 = PASS, 1 = verification FAIL, 2 = usage/environment error.
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
    import shapely
    from shapely.geometry import LineString, Point, Polygon, box
    from shapely.affinity import rotate, translate
    from shapely.strtree import STRtree
    from shapely.ops import unary_union
except ImportError:
    sys.exit("ERROR: shapely>=2.0 is required (pip install 'shapely>=2.0')")

CYCLE = "Cycle 15"
EPS = 1e-4  # mm; KiCad internal resolution is 1 nm, allow float noise


# ---------------------------------------------------------------- S-expr parser
_TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+')


def parse_sexpr(text: str):
    stack, cur = [], []
    for m in _TOKEN.finditer(text):
        t = m.group(0)
        if t == "(":
            stack.append(cur)
            cur = []
        elif t == ")":
            done, cur = cur, stack.pop()
            cur.append(done)
        elif t.startswith('"'):
            cur.append(bytes(t[1:-1], "utf-8").decode("unicode_escape"))
        else:
            cur.append(t)
    if stack:
        raise ValueError("Unbalanced parentheses in board file")
    return cur[0]


def children(node, name):
    return [c for c in node[1:] if isinstance(c, list) and c and c[0] == name]


def child(node, name):
    r = children(node, name)
    return r[0] if r else None


def fval(node, name, idx=1, default=None):
    c = child(node, name)
    return float(c[idx]) if c is not None and len(c) > idx else default


# ---------------------------------------------------------------- data model
@dataclass
class Copper:
    kind: str            # track | arc | via | pad | zone
    net: int
    layers: frozenset
    geom: object
    ref: str = ""        # human-readable identifier
    is_pad: bool = False
    loc: tuple = (0.0, 0.0)


@dataclass
class Board:
    copper_layers: list = field(default_factory=list)
    nets: dict = field(default_factory=dict)       # id -> name
    items: list = field(default_factory=list)
    track_widths: list = field(default_factory=list)  # (width, ref, loc)
    warnings: list = field(default_factory=list)


def kicad_rotate(x, y, deg):
    """KiCad rotation in Y-down coordinates (positive = CCW on screen)."""
    a = math.radians(deg)
    return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)


def arc_points(s, m, e, seg_len=0.02):
    (x1, y1), (x2, y2), (x3, y3) = s, m, e
    d = 2 * (x1 * (y2 - y3) + x2 