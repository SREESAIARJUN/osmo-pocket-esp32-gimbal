# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space (Cycle 10)

This script runs two independent checks and combines the results into one report.

1. **Native geometry checks** parse the `.kicad_pcb` file directly and need only the Python standard library:
   - **Netlist continuity:** finds split nets, unrouted pads and dangling tracks.
   - **Minimum trace width:** every track must be at least 0.1 mm.
   - **Copper clearance:** copper of different nets on the same layer must be at least 0.1 mm apart. This covers tracks, arcs, vias, pads and zone fills.
   - **Via annular ring and drill sanity.**
   - **Netlist parity (optional):** if you pass an exported netlist (`.net`), every pad's net is compared against it.
2. **KiCad's own DRC (optional):** if `kicad-cli` is installed, the script runs `kicad-cli pcb drc` with schematic parity and merges its violations into the report.

The script exits with code `0` on pass, `1` on any violation and `2` on a usage or parse error, so you can use it as a CI gate.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle10.py — KiCad 8 PCB verification (Cycle 10)

Checks:
  * Netlist continuity (split nets / unrouted pads / dangling tracks)
  * Minimum trace width        >= 0.100 mm
  * Minimum copper clearance   >= 0.100 mm (tracks, arcs, vias, pads, zone fills)
  * Via annular ring / drill sanity
  * Optional netlist parity against an exported .net file
  * Optional native `kicad-cli pcb drc` run, merged into the report

Usage:
  python verify_pcb_cycle10.py board.kicad_pcb [--netlist board.net]
         [--min-width 0.1] [--min-clearance 0.1] [--min-annular 0.1]
         [--no-kicad-cli] [--report report_cycle10.json]

Exit codes: 0 = PASS, 1 = violations found, 2 = usage/parse error
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

CYCLE = 10
EPS = 1e-4  # mm numerical tolerance (0.1 µm)


# ───────────────────────────── S-expression parser ─────────────────────────────
def parse_sexpr(text: str):
    tokens, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c in "()":
            tokens.append(c); i += 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 1
                buf.append(text[j]); j += 1
            tokens.append(("str", "".join(buf))); i = j + 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()"':
                j += 1
            tokens.append(text[i:j]); i = j

    stack, cur = [], []
    for t in tokens:
        if t == "(":
            stack.append(cur); cur = []
        elif t == ")":
            done = cur; cur = stack.pop(); cur.append(done)
        else:
            cur.append(t[1] if isinstance(t, tuple) else t)
    if stack:
        raise ValueError("Unbalanced parentheses in S-expression")
    return cur[0] if len(cur) == 1 else cur


def find(node, key):
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def find_all(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def fnum(node, key, idx=1, default=None):
    f = find(node, key)
    return float(f[idx]) if f and len(f) > idx else default


# ───────────────────────────────── Geometry ──────────────────────────────────
def pt_seg_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    if L2 < 1e-18:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def _orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def segs_intersect(a, b, c, d):
    d1, d2 = _orient(c, d, a), _orient(c, d, b)
    d3, d4 = _orient(a, b, c), _orient(a, b, d)
    return (d1 * d2 < 0) and (d3 * d4 < 0)


def seg_seg_dist(a, b, c, d):
    if segs_intersect(a, b, c, d):
        return 0.0
    return min(pt_seg_dist(a, c, d), pt_seg_dist(b, c, d),
               pt_seg_dist(c, a, b), pt_seg_dist(d, a, b))


def point_in_poly(p, poly):
    x, y = p; inside = False; n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            if x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
                inside = not inside
    return inside


def rotate(x, y, deg):
    # KiCad: Y axis down, positive angle = CCW on screen
    r = math.radians(deg); c, s = math.cos(r), math.sin(r)
    return x * c + y * s, -x * s + y * c


def arc_to_polyline(start, mid, end, max_seg=0.05):
    (x1, y1), (x2, y2), (x3, y3) = start, mid, end
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-12:
        return [start, end]
    ux = ((x1**2 + y1**2) * (y2 - y3) + (x2**2 + y2**2) * (y3 - y1) + (x3**2 + y3**2) * (y1 - y2)) / d
    uy = ((x1**2 + y1**2) * (x3 - x2) + (x2**2 + y2**2) * (x1 - x3) + (x3**2 + y3**2) * (x2 - x1)) / d
    r = math.hypot(x1 - ux, y1 - uy)
    a1, a2, a3 = (math.atan2(p[1] - uy, p[0] - ux) for p in (start, mid, end))

    def norm(a):
        return a % (2 * math.pi)

    sweep = norm(a3 - a1)
    if norm(a2 - a1) > sweep:  # mid must lie on the sweep; else go the other way
        sweep -= 2 * math.pi
    nseg = max(2, int(abs(sweep) * r / max_seg) + 1)
    return [(ux + r * math.cos(a1 + sweep * k / nseg),
             uy + r * math.sin(a1 + sweep * k / nseg)) for k in range(nseg + 1)]


# ─────────────────────────────── Board model ────────────────────────────────
@dataclass
class Shape:
    """Copper primitive: a capsule (polyline + radius) or a polygon (+ radius inflation)."""
    kind: str          # 'track' | 'arc' | 'via' | 'pad' | 'zone'
    net: int
    layers: frozenset
    path: list         # capsule spine