# KiCad 8 PCB Verification: Netlist Continuity and 0.1 mm Trace/Space DRC

The script below reads a `.kicad_pcb` file directly and runs its own geometric checks. If `kicad-cli` is installed, it can also run KiCad's built-in DRC and compare the two results.

## What it checks

| Check | Method |
|---|---|
| **Continuity (opens)** | Builds a union-find graph of every copper object on each net: tracks, arcs, vias, pads and zone fills. It flags any net whose pads end up in more than one connected group. |
| **Shorts** | Flags copper from different nets that touches or overlaps on the same layer. |
| **Clearance** | Measures the edge-to-edge distance between copper of different nets on each layer and requires at least **0.100 mm**. |
| **Trace width** | Requires every track/arc width to be at least **0.100 mm**. It also checks each zone's `min_thickness`. |
| **Dangling copper** | Reports tracks or vias that connect to no pad, as warnings. |
| **Cross-check (optional)** | Runs `kicad-cli pcb drc` and folds in KiCad's own violation and unconnected-item counts. |

## Requirements

```bash
pip install "shapely>=2.0"
```

## Script: `verify_pcb.py`

```python
#!/usr/bin/env python3
"""
verify_pcb.py - KiCad 8 PCB netlist continuity + DRC clearance verification
Target rule set: 0.1 mm minimum trace width / 0.1 mm minimum spacing (Cycle 26)

Usage:
    python verify_pcb.py board.kicad_pcb [--min-trace 0.1] [--min-space 0.1]
                         [--kicad-cli] [--json report.json]

Exit codes: 0 = pass, 1 = violations found, 2 = input/tool error
"""
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
    from shapely import affinity
    from shapely.strtree import STRtree
except ImportError:
    sys.exit("ERROR: shapely>=2.0 is required (pip install shapely)")

CYCLE = 26
EPS = 1e-4        # mm, numeric tolerance for geometry comparisons
QUAD_SEGS = 16    # circle approximation resolution

# --------------------------------------------------------------------------
# S-expression parsing
# --------------------------------------------------------------------------
_TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+')


def parse_sexpr(text):
    stack = [[]]
    for m in _TOKEN.finditer(text):
        t = m.group(0)
        if t == "(":
            stack.append([])
        elif t == ")":
            node = stack.pop()
            stack[-1].