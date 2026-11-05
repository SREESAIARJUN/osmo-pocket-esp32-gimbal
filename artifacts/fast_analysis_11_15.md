# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space (Cycle 11)

This script runs two layers of checks on a KiCad 8 board:

1. **An independent geometric engine.** It parses the `.kicad_pcb` file directly, with no `pcbnew` dependency. It checks:
   - Track widths of at least 0.1 mm
   - Copper-to-copper spacing of at least 0.1 mm between different nets on shared layers
   - Netlist continuity: each net must be one connected copper cluster
2. **A cross-check against KiCad's own DRC** via `kicad-cli pcb drc`, when `kicad-cli` is available. It also audits the `.kicad_pro` rule settings, so a DRC configured looser than 0.1 mm gets flagged.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle11.py — KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Usage:
    python verify_pcb_cycle11.py board.kicad_pcb [--min-width 0.1] [--min-space 0.1]
                                 [--report report.json] [--no-kicad-cli]

Exit codes: 0 = PASS, 1 = FAIL (violations found), 2 = tool/parse error.

Independent checks (pure Python, parses the .kicad_pcb directly):
  * Track/arc width >= min width
  * Copper clearance between different nets on shared copper layers
    (tracks, arcs, vias, pads, filled zone polygons)
  * Net continuity: every net's copper forms a single connected cluster
Cross-checks (optional):
  * kicad-cli DRC (clearance, unconnected items, schematic parity)
  * .kicad_pro design rules not looser than the Cycle 11 requirement

Known simplifications (conservative or documented):
  * roundrect/trapezoid/custom pads are modeled as their bounding rectangle
    (pessimistic: may over-report clearance near rounded corners)
  * hole-to-copper and board-edge clearances are delegated to kicad-cli DRC
"""
from __future__ import annotations

import argparse
import json
import math
import re
