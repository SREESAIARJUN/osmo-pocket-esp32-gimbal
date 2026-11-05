# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC (Cycle 4)

The script runs four groups of checks, each reported as PASS or FAIL.

1. **Rule configuration audit.** It checks that the board's design rules and every netclass enforce at least 0.1 mm track width and clearance. If a rule is set lower, KiCad's DRC would pass designs that violate the spec.
2. **Official KiCad DRC.** It runs `kicad-cli pcb drc` with schematic parity enabled and parses the JSON report. This covers clearance and track-width violations, unrouted connections (continuity), and schematic/PCB parity errors.
3. **Independent netlist continuity.** It exports the schematic netlist and compares its pin groupings against the pad nets in the `.kicad_pcb`. The comparison groups pins by net, so it still works when net names are formatted differently.
4. **Independent geometric check.** It parses the copper geometry directly and checks the minimum width of every segment and arc. It also checks the copper-to-copper clearance between different-net tracks, arcs and vias, using a spatial hash so it stays fast.

```python
#!/usr/bin/env python3
"""
verify_pcb_cycle4.py — KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Cycle 4 acceptance criteria:
  * Minimum track width  >= 0.100 mm
  * Minimum clearance    >= 0.100 mm (copper-to-copper, different nets)
  * Zero unrouted connections
  * Zero schematic <-> PCB parity errors
  * Schematic netlist pin groupings == PCB pad net groupings

Usage:
  python verify_pcb_cycle4.py board.kicad_pcb [--sch board.kicad_sch]
                              [--pro board.kicad_pro] [--report out.json]
                              [--kicad-cli kicad-cli] [--skip-kicad-cli]

Exit code: 0 = all checks pass, 1 = failures, 2 = usage/tool error.
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

CYCLE = "Cycle 4"
MIN_TRACE_MM = 0.100
MIN_SPACE_MM = 0.100
EPS = 1e-6  # mm; absorbs float round-off from file serialization


# --------------------------------------------------------------------------- #
# S-expression parser (KiCad .kicad_pcb / .net format)
# --------------------------------------------------------------------------- #
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
            tokens.append(("STR", "".join(buf))); i = j + 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()"':
                j += 1
            tokens.append(text[i:j]); i = j

    stack, root = [], []
    cur = root
    for t in tokens:
        if t == "(":
            new = []; cur.append(new); stack.append(cur); cur = new
        elif t == ")":
            cur = stack.pop()
        else:
            cur.append(t[1] if isinstance(t, tuple) else t)
    return root[0] if len(root) == 1 else root


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
# Result bookkeeping
# --------------------------------------------------------------------------- #
@dataclass
class CheckResult:
    name: str
    passed: bool = True
    details: list = field(default_factory=list)
    skipped: bool = False

    def fail(self, msg):
        self.passed = False
        self.details.append(msg)


# --------------------------------------------------------------------------- #
# PCB model
# --------------------------------------------------------------------------- #
@dataclass
class Seg:
    x1: float; y1: float; x2: float; y2: float
    width: float; layer: str; net: int; kind: str; uid: str


@dataclass
class Via:
    x: float; y: float; size: float; layers: list; net: int; uid: str


def load_pcb(path: Path):
    tree = parse_sexpr(path.read_text(encoding="utf-8"))
    nets = {int(n[1]): n[2] for n in children(tree, "net")}
    copper_layers = [l[1] for l in (child(tree, "layers") or [])[1:]
                     if isinstance(l, list) and len(l) > 1 and l[1].endswith(".Cu")]

    segs, vias, pads = [], [], {}  # pads: (ref, pin) -> net name

    for s in children(tree, "segment"):
        st, en = child(s, "start"), child(s, "end")
        segs.append(Seg(float(st[1]), float(st[2]), float(en[1]), float(en[2]),
                        fval(s, "width"), child(s, "layer")[1],
                        int(child(s, "net")[1]), "segment",
                        (child(s, "uuid") or ["", ""])[1]))

    for a in children(tree, "arc"):
        st, md, en = child(a, "start"), child(a, "mid"), child(a, "end")
        pts = arc_to_polyline((float(st[1]), float(st[2])),
                              (float(md[1]), float(md[2])),
                              (float(en[1]), float(en[2])))
        w, layer, net = fval(a, "width"), child(a, "layer")[1], int(child(a, "net")[1])
        uid = (child(a, "uuid") or ["", ""])[1]
        for p, q in zip(pts, pts[1:]):
            segs.append(Seg(p[0], p[1], q[0], q[1], w, layer, net, "arc", uid))

    for v in children(tree, "via"):
        at = child(v, "at")