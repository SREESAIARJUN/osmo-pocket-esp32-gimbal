# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space (Cycle 24)

The script below runs two independent checks and only passes the board if both agree:

1. **Native geometric checker.** It parses the `.kicad_pcb` file directly and builds copper geometry with Shapely. It then checks:
   - track and arc widths of at least 0.1 mm
   - copper-to-copper clearance of at least 0.1 mm between different nets, on every copper layer
   - net continuity, using union-find over pads, tracks, vias and filled zones
   - optionally, schematic-to-PCB netlist parity against a KiCad `.net` export
2. **KiCad's own DRC** through `kicad-cli pcb drc` (optional). It also confirms that the project's design rules actually enforce 0.1 mm. If the project allows less than that, KiCad's DRC would silently pass violations.

```python
#!/usr/bin/env python3
"""
kicad_verify.py  --  Cycle 24 PCB verification
KiCad 8: netlist continuity + 0.1 mm minimum trace/space compliance.

Usage:
  python kicad_verify.py board.kicad_pcb [--netlist board.net] [--kicad-cli kicad-cli]
                         [--min-width 0.1] [--min-space 0.1] [--report report.json]

Exit codes: 0 = PASS, 1 = FAIL (violations), 2 = tool/input error

Requires: shapely >= 2.0   (pip install shapely)
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
    from shapely.affinity import rotate as sh_rotate, translate as sh_translate
    from shapely.strtree import STRtree
    from shapely.validation import make_valid
except ImportError:
    sys.exit("ERROR: shapely>=2.0 required (pip install shapely)")

if int(shapely.__version__.split(".")[0]) < 2:
    sys.exit("ERROR: shapely>=2.0 required")

CYCLE = "24"
EPS = 1e-4          # mm, numeric tolerance (KiCad internal unit is 1 nm)
ARC_SEGS = 32       # tessellation for arcs/circles


# --------------------------------------------------------------------------
# S-expression parsing
# --------------------------------------------------------------------------
_TOKEN = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+')


def parse_sexpr(text: str):
    stack, cur = [], []
    for tok in _TOKEN.findall(text):
        if tok == "(":
            stack.append(cur)
            cur = []
        elif tok == ")":
            done = cur
            cur = stack.pop()
            cur.append(done)
        else:
            if tok.startswith('"'):
                tok = tok[1:-1].replace('\\"', '"').replace("\\\\", "\\")
            cur.append(tok)
    if stack:
        raise ValueError("Unbalanced parentheses in S-expression")
    return cur[0] if len(cur) == 1