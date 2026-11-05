# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space (Cycle 22)

The script runs two independent checks:

1. **A native geometry engine.** It parses the `.kicad_pcb` file directly and checks:
   - Minimum trace width, via annular ring, and copper-to-copper clearance on each layer.
   - Per-net continuity. It builds connectivity islands from tracks, arcs, vias, pads and filled zones.
2. **KiCad's own DRC.** If `kicad-cli` is on `PATH`, the script runs it with `--schematic-parity` and merges its JSON results.

The script produces a console summary, a JSON report and a CI-friendly exit code.

```python
#!/usr/bin/env python3
"""
verify_pcb.py — KiCad 8 netlist continuity + 0.1 mm trace/space verification
Verification cycle: 22

Usage:
    python verify_pcb.py board.kicad_pcb [--min-trace 0.1] [--min-space 0.1]
                         [--min-annular 0.1] [--no-kicad-cli]
                         [--report cycle22_report.json]

Exit codes:
    0 = PASS, 1 = violations found, 2 = input/tool error
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

CYCLE = 22
EPS = 1e-6  # mm tolerance for float noise


# ─────────────────────────── S-expression parser ────────────────────────────
_TOKEN = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+')


def parse_sexpr(text: str):
    stack, cur = [], []
    for m in _TOKEN.finditer(text):
        t = m.group(0)
        if t == "(":
            stack.append(cur)
            cur = []
        elif t == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
        elif t.startswith('"'):
            cur.append(t[1:-1].replace('\\"', '"'))
        else:
            cur.append(t)
    if stack:
        raise ValueError("Unbalanced parentheses in PCB file")
    return cur[0]


def children(node, tag):
    return [c for c in node if isinstance(c, list) and c and c[0] == tag]


def child(node, tag):
    for c in node:
        if isinstance(c, list) and c and c[0] == tag:
            return c
    return None


def fval(node, tag, idx=1, default=None):
    c = child(node, tag)
    return float(c[idx]) if c is not None and len(c) > idx else default


# ─────────────────────────────── Geometry ───────────────────────────────────
Pt = tuple[float, float]


def pt_seg_dist(p: Pt, a: Pt, b: Pt) -> float:
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    if L2 < 1e-18:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def _orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def segs_intersect(a, b, c, d) -> bool:
    d1, d2 = _orient(c, d, a), _orient(c, d, b)
    d3, d4 = _orient(a, b, c), _orient(a, b, d)
    return (d1 * d2 < 0) and (d3 * d4 < 0)


def seg_seg_dist(a, b, c, d) -> float:
    if segs_intersect(a, b, c, d):
        return 0.0
    return min(pt_seg_dist(a, c, d), pt_seg_dist(b, c, d),
               pt_seg_dist(c, a, b), pt_seg_dist(d, a, b))


def point_in_poly(p: Pt, poly: list[Pt]) -> bool:
    x, y = p
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            if x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
                inside = not inside
    return inside


def poly_edges(poly):
    return [(poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly))]


def rotate(x, y, deg):
    """KiCad rotation (Y axis points down, positive angle = CCW on screen)."""
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return x * c + y * s, -x * s + y * c


# ───────────────────────────── Copper shapes ────────────────────────────────
@dataclass
class Shape:
    """A copper primitive: a 'capsule' (a segment of centerline points
    swept by a radius) and/or a polygon outline."""
    kind: str               # track | arc | via | pad | zone
    net: int
    layers: frozenset[str]
    ref: str
    a: Pt | None = None
    b: Pt | None = None
    radius: float = 0.0
    poly: list[Pt] | None = None
    width: float = 0.0
    bbox: tuple = field(default=(0, 0, 0, 0))

    def finalize(self):
        pts = list(self.poly or []) + [p for p in (self.a, self.b) if p]
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        r = self.radius
        self.bbox = (min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r)
        return self

    def where(self) -> str:
        p = self.a or (self.poly[0] if self.poly else (0, 0))
        return f"({p[0]:.3f}, {p[1]:.3f})"


def shape_dist(s: Shape, t: Shape) -> float:
    """Edge-to-edge copper distance (<=0 means touching/overlapping)."""
    if s.poly is None and t.poly is None:
        return seg_seg_dist(s.a, s.b, t.a, t.b) - s.radius - t.radius
    if s.poly is not None and t.poly is not None:
        if any(point_in_poly(p, t.poly) for p in s.poly) or \
           any(point_in_poly(p, s.poly) for p in t.poly):
            return -1.0
        return min(seg_seg_dist(a, b, c, d)
                   for a, b in poly_edges(s.poly)
                   for c, d in poly_edges(t.poly))
    cap, pol = (s, t) if s.poly is None else (t, s)
    if point_in_poly(cap.a, pol.poly) or point_in_poly(cap.b, pol.poly):
        return -1.0
    d = min(seg_seg_dist(cap.a, cap.b, c, e) for c, e in poly_edges(pol.poly))
    return d - cap.radius


# ───────────────────────────── Board loader ─────────────────────────────────
class Board:
    def __init__(self, path: Path):
        self.root = parse_sexpr(path.read_text(encoding="utf-8"))
        if not self.root or self.root[0] != "kicad_pcb":
            raise ValueError("Not a kicad_pcb file")
        ver = child(self.root, "version")
        self.version = int(ver[1]) if ver else 0
        self.copper_layers = self._copper_layers()
        self.nets = self._nets()
        self.shapes: list[Shape] = []
        self.track_widths: list[tuple[float, Shape]] = []
        self.via_rings: list[tuple[float, Shape]] = []
        self._load_tracks()
        self._load_vias()
        self._load_pads()
        self._load_zones()

    def _copper_layers(self):
        layers = child(self.root, "layers")
        out = [l[1] for l in layers[1:] if isinstance(l, list) and l[1].endswith(".Cu")]
        return out or ["F.Cu", "B.Cu"]

    def _nets(self):
        return {int(n[1]): n[2] for n in children(self.root, "net")}

    def _expand_layers(self, names):
