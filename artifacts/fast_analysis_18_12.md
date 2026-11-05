# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC (Cycle 18)

The script runs two independent verification paths and cross-checks them:

1. **Native geometric analysis.** It parses the `.kicad_pcb` file directly, then:
   - checks track widths
   - checks copper-to-copper clearance between different nets
   - detects shorts
   - checks netlist continuity using union-find connectivity over pads, tracks, arcs, vias and zone fills
2. **Authoritative KiCad DRC.** It calls `kicad-cli pcb drc` (KiCad 8) with schematic parity. It also checks that the board's own rule settings are not looser than 0.1 mm.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle18.py — KiCad 8 PCB verification (Cycle 18)

Checks:
  [1] Design-rule settings in .kicad_pcb/.kicad_pro are >= 0.1 mm trace/space
  [2] Every track/arc width >= 0.1 mm
  [3] Copper clearance between different nets >= 0.1 mm (same layer)
  [4] No shorts (different nets touching)
  [5] Netlist continuity: all pads of each net form one connected island
  [6] (optional) kicad-cli DRC + schematic parity, JSON report parsed

Usage:
  python verify_pcb_cycle18.py board.kicad_pcb [--min-trace 0.1] [--min-space 0.1]
         [--no-kicad-cli] [--schematic-parity] [--report out.json]

Exit code: 0 = PASS, 1 = FAIL, 2 = usage / parse error.
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

CYCLE = 18
EPS = 1e-6           # numeric tolerance (mm)
TOUCH_TOL = 1e-4     # "touching" tolerance for connectivity (mm)
ARC_SEGMENTS = 16    # arc tessellation

# --------------------------------------------------------------------------
# S-expression parser
# --------------------------------------------------------------------------
_TOKEN_RE = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))')


def parse_sexpr(text: str):
    stack, cur = [], []
    pos, n = 0, len(text)
    while pos < n:
        m = _TOKEN_RE.match(text, pos)
        if not m:
            if text[pos:].strip() == "":
                break
            raise ValueError(f"S-expr parse error at offset {pos}")
        pos = m.end()
        if m.group(1):
            stack.append(cur)
            cur = []
        elif m.group(2):
            done = cur
            cur = stack.pop()
            cur.append(done)
        elif m.group(3) is not None:
            cur.append(bytes(m.group(3), "utf-8").decode("unicode_escape"))
        elif m.group(4) is not None:
            cur.append(m.group(4))
    if stack:
        raise ValueError("Unbalanced parentheses in PCB file")
    return cur[0] if len(cur) == 1 else cur


def children(node, tag):
    return [c for c in node if isinstance(c, list) and c and c[0] == tag]


def child(node, tag):
    for c in node:
        if isinstance(c, list) and c and c[0] == tag:
            return c
    return None


def fval(node, tag, idx=1, default=None):
    c = child(node, tag)
    if c is None or len(c) <= idx:
        return default
    try:
        return float(c[idx])
    except (TypeError, ValueError):
        return default


# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------
def rotate(x, y, deg):
    """KiCad rotation (Y axis down, positive angle = CCW on screen)."""
    if not deg:
        return x, y
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return x * c + y * s, -x * s + y * c


def pt_seg_dist(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    if L2 < EPS * EPS:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx