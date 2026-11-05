# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Clearance (Cycle 12)

The script `verify_pcb_cycle12.py` runs two independent layers of checks:

1. **Authoritative check (`kicad-cli`).** It runs KiCad 8's own DRC with schematic parity, then parses the JSON report. This covers clearance, track width, unconnected items, and netlist mismatches.
2. **Independent check (pure Python).** It parses the `.kicad_pcb` and `.kicad_pro` files directly to confirm that:
   - the rule configuration meets the 0.1 mm minimum,
   - every track meets the minimum width,
   - copper spacing on each layer passes a geometric clearance scan,
   - the track, via and pad graph of each net is continuous.

   This catches cases where the DRC rules were loosened to make the board "pass".

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle12.py — KiCad 8 PCB verification (Cycle 12)

Checks:
  A. kicad-cli DRC (JSON) with schematic parity:
       - zero clearance / track-width / short / copper-edge errors
       - zero unconnected items (netlist continuity)
       - zero schematic-parity mismatches
  B. Independent static checks on .kicad_pcb / .kicad_pro:
       - design rules: min_clearance >= 0.1 mm, min_track_width >= 0.1 mm
       - every net class: clearance >= 0.1 mm, track_width >= 0.1 mm
       - every track segment/arc width >= 0.1 mm
       - geometric copper-to-copper clearance between different nets on the same layer
       - per-net graph continuity of tracks/vias/pads (nets with zones are deferred to DRC)

Usage:
  python verify_pcb_cycle12.py board.kicad_pcb [--sch board.kicad_sch]
        [--min-trace 0.1] [--min-space 0.1] [--report out.json] [--skip-cli]

Exit code: 0 = PASS, 1 = FAIL, 2 = tool/setup error
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
from dataclasses import dataclass, field, asdict
from pathlib import Path

CYCLE = "Cycle 12"
EPS = 1e-4  # mm tolerance for float comparisons


# ----------------------------------------------------------------------------
# Results bookkeeping
# ----------------------------------------------------------------------------
@dataclass
class CheckResult:
    name: str
    passed: bool
    details: list = field(default_factory=list)
    count: int = 0


class Report:
    def __init__(self, board: Path):
        self.board = str(board)
        self.results: list[CheckResult] = []

    def add(self, name, issues, max_listed=50):
        r = CheckResult(name, passed=not issues, details=issues[:max_listed], count=len(issues))
        self.results.append(r)
        status = "PASS" if r.passed else "FAIL"
        print(f"[{status}] {name}" + ("" if r.passed else f"  ({r.count} issue(s))"))
        for d in r.details[:10]:
            print(f"        - {d}")
        if r.count > 10:
            print(f"        ... {r.count - 10} more (see report)")

    @property
    def passed(self):
        return all(r.passed for r in self.results)

    def to_json(self):
        return {
            "cycle": CYCLE,
            "board": self.board,
            "overall": "PASS" if self.passed else "FAIL",
            "checks": [asdict(r) for r in self.results],
        }


# ----------------------------------------------------------------------------
# S-expression parser (sufficient for KiCad 8 files)
# ----------------------------------------------------------------------------
def parse_sexpr(text: str):
    tokens, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "()":
            tokens.append(c); i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j + 1]); j += 2
                else:
                    buf.append(text[j]); j += 1
            tokens.append(("STR", "".join(buf))); i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '()"':
                j += 1
            tokens.append(text[i:j]); i = j

    stack, root = [], []
    cur = root
    for t in tokens:
        if t == "(":
            new = []
            cur.append(new); stack.append(cur); cur = new
        elif t == ")":
            cur = stack.pop()
        else:
            cur.append(t[1] if isinstance(t, tuple) else t)
    return root[0]


def children(node, tag):
    return [c for c in node if isinstance(c, list) and c and c[0] == tag]


def child(node, tag):
    for c in node:
        if isinstance(c, list) and c and c[0] == tag:
            return c
    return None


def fnum(x):
    return float(x)


# ----------------------------------------------------------------------------
# Geometry
# ----------------------------------------------------------------------------
def seg_seg_dist(p1, p2, q1, q2):
    """Minimum distance between 2D segments p1p2 and q1q2."""
    def pt_seg(p, a, b):
        ax, ay = a; bx, by = b; px, py = p
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        if L2 == 0:
            return math.hypot(px - ax, py - ay)
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
        return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

    def ccw(a, b, c):
        return (c[1] - a[1]) * (b[0] - a[0]) - (b[1] - a[1]) * (c[0] - a[0])

    d1, d2 = ccw(q1, q2, p1), ccw(q1, q2, p2)
    d3, d4 = ccw(p1, p2, q1), ccw(p1, p2, q2)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)) and d1 * d2 != 0 and d3 * d4 != 0:
        return 0.0
    return min(pt_seg(p1, q1, q2), pt_seg(p2, q1, q2),
               pt_seg(q1, p1, p2), pt_seg(q2, p1, p2))


def rotate(x, y, deg):
    # KiCad rotation is clockwise-positive in screen (Y-down) coordinates
    r = math.radians(-deg)
    return x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)


def arc_to_polyline(start, mid, end, max_seg=0.1):
    """Approximate a 3-point arc with line segments."""
    (x1, y1), (x2, y2), (x3, y3) = start, mid, end
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-12:
        return [start, end]
    ux = ((x1**2 + y1**2) * (y2 - y3) + (x2**2 + y2**2) * (y3 - y1) + (x3**2 + y3**2) * (y1 - y2)) / d
    uy = ((x1**2 + y1**2) * (x3 - x2) + (x2**2 + y2**2) * (x1 - x3) + (x3**2 + y3**2) * (x2 - x1)) /