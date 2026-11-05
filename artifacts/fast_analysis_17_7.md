# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC (Cycle 17)

The script runs two checks and fails the cycle if either one fails:

1. **Authoritative check:** It runs `kicad-cli pcb drc` with schematic parity, using KiCad's own DRC engine and the zone fills.
2. **Independent check:** It parses the `.kicad_pcb` file directly in pure Python with no extra dependencies. It verifies:
   - every track and via meets the 0.1 mm minimum width/drill,
   - copper-to-copper clearance between different nets is at least 0.1 mm on each layer,
   - every net forms one connected copper island (continuity).

It also confirms that the board's own rule setup (`.kicad_pro`) does not allow anything below 0.1 mm.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle17.py - KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Usage:
    python verify_pcb_cycle17.py board.kicad_pcb [--min-track 0.1] [--min-space 0.1]
                                 [--kicad-cli kicad-cli] [--skip-cli] [--json report.json]

Exit codes:
    0 = PASS, 1 = verification FAIL, 2 = tool/usage error
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

CYCLE = "Cycle 17"
EPS = 1e-4  # mm, numeric tolerance (KiCad internal unit is 1 nm)

# --------------------------------------------------------------------------- #
# S-expression parser
# --------------------------------------------------------------------------- #
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
    return float(c[idx]) if c is not None and len(c) > idx else default


# --------------------------------------------------------------------------- #
# Geometry primitives: everything copper is modelled as a "capsule"
# (segment p0-p1 swept by radius r) or as a filled polygon (zones).
# --------------------------------------------------------------------------- #
@dataclass
class Capsule:
    kind: str            # track / arc / via / pad
    net: int
    layers: frozenset
    x0: float
    y0: float
    x1: float
    y1: float
    r: float
    ref: str = ""
    exact: bool = True   # False for rect/roundrect pad approximation

    def bbox(self):
        return (min(self.x0, self.x1) - self.r, min(self.y0, self.y1) - self.r,
                max(self.x0, self.x1) + self.r, max(self.y0, self.y1) + self.r)


@dataclass
class ZonePoly:
    net: int
    layer: str
    pts: list = field(default_factory=list)

    def bbox(self):
        xs = [p[0] for p in self.pts]
        ys = [p[1] for p in self.pts]
        return min(xs), min(ys), max(xs), max(ys)


def _pt_seg_d2(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    qx, qy = ax + t * dx, ay + t * dy
    return (px - qx) ** 2 + (py - qy) ** 2


def _segs_intersect(a, b, c, d):
    def orient(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    o1, o2 = orient(a, b, c), orient(a, b, d)
    o3, o4 = orient(c, d, a), orient(c, d, b)
    return (o1 * o2 < 0) and (o3 * o4 < 0)


def seg_seg_dist(a0, a1, b0, b1):
    if _segs_intersect(a0, a1, b0, b1):
        return 0.0
    return math.sqrt(min(
        _pt_seg_d2(*a0, *b0, *b1), _pt_seg_d2(*a1, *b0, *b1),
        _pt_seg_d2(*b0, *a0, *a1), _pt_seg_d2(*b1, *a0, *a1)))


def capsule_gap(a: Capsule, b: Capsule) -> float:
    """Edge-to-edge copper gap (negative = overlap)."""
    d = seg_seg_dist((a.x0, a.y0), (a.x1, a.y1), (b.x0, b.y0), (b.x1, b.y1))
    return d - a.r - b.r


def point_in_poly(x, y, pts):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-30) + xi:
            inside = not inside
        j = i
    return inside


def capsule_touches_poly(c: Capsule, z: ZonePoly) -> bool:
    if point_in_poly(c.x0, c.y0, z.pts) or point_in_poly(c.x1, c.y1, z.pts):
        return True
    n = len(z.pts)
    for i in range(n):
        p, q = z.pts[i], z.pts[(i + 1) % n]
        if seg_seg_dist((c.x0, c.y0), (c.x1, c.y1), p, q) <= c.r + EPS:
            return True
