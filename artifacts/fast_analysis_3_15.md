# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm DRC (Cycle 3)

The script `verify_pcb_cycle3.py` runs two independent checks on a KiCad 8 board:

1. **A geometric engine written from scratch in Python.** It parses the `.kicad_pcb` file directly and needs no third-party packages. It checks:
   - track widths (minimum 0.1 mm)
   - copper-to-copper spacing between different nets (minimum 0.1 mm), including shorts
   - per-net copper continuity (open nets and dangling copper)
   - pad-to-net assignments against an exported schematic netlist
2. **An optional cross-check with `kicad-cli`.** It runs KiCad's own DRC with schematic parity and adds those results to the report.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle3.py - KiCad 8 PCB verification, Cycle 3

Checks
  1. Track width       >= MIN_TRACE (default 0.100 mm)
  2. Copper clearance  >= MIN_SPACE (default 0.100 mm) between different nets
                          (negative distance = short circuit)
  3. Net continuity    every net's pads form ONE connected copper island
  4. Netlist parity    board pad->net assignment matches schematic netlist
  5. (optional) KiCad's own DRC via kicad-cli, incl. schematic parity
  6. Project rule sanity: .kicad_pro rules are not looser than the targets

Usage
  python verify_pcb_cycle3.py board.kicad_pcb
         [--netlist board.net]          # kicad-cli sch export netlist board.kicad_sch
         [--kicad-cli kicad-cli]        # also run native DRC
         [--min-trace 0.1] [--min-space 0.1]
         [--report report.json]

Exit code: 0 = PASS, 1 = FAIL, 2 = usage / parse error
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

CYCLE = 3
EPS = 1e-4            # mm: geometric tolerance (KiCad internal unit is 1 nm)
GRID = 2.0            # mm: spatial hash cell size

# --------------------------------------------------------------------------- #