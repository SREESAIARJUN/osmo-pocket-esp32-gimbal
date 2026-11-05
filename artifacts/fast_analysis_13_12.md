# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC (Cycle 13)

The script runs **three independent checks**. A board passes only if all of them pass:

1. **Native KiCad DRC.** It runs `kicad-cli pcb drc` with the JSON output, all severities and schematic parity enabled.
2. **Independent geometric check.** It parses the `.kicad_pcb` file and checks:
   - Minimum track width of 0.1 mm.
   - Minimum copper-to-copper clearance of 0.1 mm.
   - Copper continuity per net, using union-find over the copper shapes.
3. **Netlist cross-check.** It compares each pad's net against the schematic netlist exported by `kicad-cli`.

It also warns if the project's design rules are configured looser than 0.1 mm. Looser rules would let the native DRC pass boards that violate the Cycle 13 limits.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle13.py — KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Usage:
    python verify_pcb_cycle13.py board.kicad_pcb [--sch board.kicad_sch]
                                 [--min-width 0.1] [--min-space 0.1]
                                 [--no-kicad-cli] [--report report.json]

Exit code: 0 = PASS, 1 = FAIL, 2 = tool/usage error.
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

CYCLE = 13
EPS = 1e-6  # mm tolerance for float comparisons


# ----------------------------------------------------------------------------
# S-expression parsing
# ----------------------------------------------------------------------------
_TOKEN_RE = re.compile(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()]+')


def parse_sexpr(text: str):
    stack, cur = [