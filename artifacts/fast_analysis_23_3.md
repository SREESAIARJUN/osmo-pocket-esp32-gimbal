# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC

The script runs three independent checks:

1. **Official DRC** via `kicad-cli pcb drc`. This includes clearance, track width, unconnected items and schematic parity.
2. **Independent geometry audit** via the `pcbnew` Python API. It checks track widths, design-rule floors, netclass values, and copper-to-copper clearance.
3. **Netlist continuity**. It compares the schematic netlist (pin to net) against PCB pads and confirms every net is fully routed.

The second check exists so the result does not depend only on the board's own rule settings. If someone relaxed the board rules to 0.05 mm, KiCad's DRC would pass, but this audit would still flag the violation.

```python
#!/usr/bin/env python3
"""
verify_pcb.py - KiCad 8 netlist continuity + 0.1 mm trace/space verification.

Usage (run with KiCad's bundled Python so `pcbnew` is importable):
    python verify_pcb.py --pcb board.kicad_pcb --sch board.kicad_sch \
        --min-trace 0.1 --min-space 0.1 --report cycle23_report.json --tag "Cycle 23"

Exit codes: 0 = PASS, 1 = FAIL (violations), 2 = tool/setup error
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

try:
    import pcbnew
    HAVE_PCBNEW = True
except ImportError:
    HAVE_PCBNEW = False

NM_PER_MM = 1_000_000
EPS_NM = 1  # 1 nm rounding tolerance


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #
class Report:
    def __init__(self, tag):
        self.data = {
            "tag": tag,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "checks": {},
            "result": "PASS",
        }

    def add(self, name, passed, details=None, warnings=None):
        self.data["checks"][name] = {
            "passed": passed,
            "details": details or [],
            "warnings": warnings or [],
        }
        if not passed:
            self.data["result"] = "FAIL"
        status = "PASS" if passed else "FAIL"
        print(f"[{status}] {name}: {len(details or [])} issue(s)")
        for d in (details or [])[:25]:
            print(f"        - {d}")
        if details and len(details) > 25:
            print(f"        ... {len(details) - 25} more (see JSON report)")
        for w in (warnings or []):
            print(f"   [WARN] {w}")

    def error(self, name, msg):
        self.data["checks"][name] = {"passed": False, "error": msg}
        self.data["result"] = "FAIL"
        print(f"[ERROR] {name}: {msg}")


def find_kicad_cli():
    cli = shutil.which("kicad-cli")
    if cli:
        return cli
    for p in (
        r"C:\Program Files\KiCad\8.0\bin\kicad-cli.exe",
        "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
        "/usr/bin/kicad-cli",
    ):
        if Path(p).exists():
            return p
    return None


def mm(nm):
    return nm / NM_PER_MM


# --------------------------------------------------------------------------- #
# Check 1: kicad-cli DRC (JSON)
# --------------------------------------------------------------------------- #
CLEARANCE_TYPES = {
    "clearance", "track_width", "copper_edge_clearance", "hole_clearance",
    "annular_width", "via_diameter", "shorting_items", "tracks_crossing",
    "solder_mask_bridge",
}


def run_kicad_drc(cli, pcb, rep, use_parity):
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "drc.json"
        cmd = [cli, "pcb", "drc", "--format", "json", "--units", "mm",
               "--severity-all", "--all-track-errors", "--output", str(out)]
        if use_parity:
            cmd.append("--schematic-parity")
        cmd.append(str(pcb))
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if not out.exists():
            rep.error("kicad_drc", f"kicad-cli failed: {proc.stderr.strip() or proc.stdout.strip()}")
            return
        drc = json.loads(out.read_text(encoding="utf-8"))

    def fmt(v):
        locs = "; ".join(
            f"{i.get('description', '')} @({i.get('pos', {}).get('x', '?')},"
            f"{i.get('pos', {}).get('y', '?')})"
            for i in v.get("items", []))
        return f"[{v.get('severity')}] {v.get('type')}: {v.get('description')} -> {locs}"

    viol = [v for v in drc.get("violations", []) if v.get("severity") != "exclusion"]
    clearance = [fmt(v) for v in viol if v.get("type") in CLEARANCE_TYPES]
    other_err = [fmt(v) for v in viol
                 if v.get("type") not in CLEARANCE_TYPES and v.get("severity") == "error"]
    other_warn = [fmt(v) for v in viol
                  if v.get("type") not in CLEARANCE_TYPES and v.get("severity") != "error"]
    unconnected = [fmt(v) for v in drc.get("unconnected_items", [])]
    parity = [fmt(v) for v in drc.get("schematic_parity", [])]

    rep.add("kicad_drc_clearance_and_width", not clearance, clearance)
    rep.add("kicad_drc_other_errors", not other_err, other_err, other_warn[:50])
    rep.add("kicad_drc_unconnected_items", not unconnected, unconnected)
    if use_parity:
        rep.add("kicad_drc_schematic_parity", not parity, parity)


# --------------------------------------------------------------------------- #
# Check 2: Independent rule / geometry audit (pcbnew API)
# --------------------------------------------------------------------------- #
def audit_design_rules(board, min_trace, min_space, rep):
    ds = board.GetDesignSettings()
    issues = []
    if ds.m_TrackMinWidth + EPS_NM < min_trace:
        issues.append(f"Board min track width {mm(ds.m_TrackMinWidth):.4f} mm < {mm(min_trace)} mm")
    if ds.m_MinClearance + EPS_NM < min_space:
        issues.append(f"Board min clearance {mm(ds.m_MinClearance):.4f} mm < {mm(min_space)} mm")

    # Netclasses (KiCad 8 API)
    try:
        ns = ds.m_NetSettings
        classes = {"Default": ns.m_DefaultNetClass}
        for name, nc in ns.m_NetClasses.items():
            classes[str(name)] = nc
        for name, nc in classes.items():
            if nc.HasTrackWidth() and nc.GetTrackWidth() + EPS_NM < min_trace:
                issues.append(f"Netclass '{name}' track width {mm(nc.GetTrackWidth()):.4f} mm")
            if nc.HasClearance() and nc.GetClearance() + EPS_NM < min_space:
                issues.append(f"Netclass '{name}' clearance {mm(nc.GetClearance()):.4f} mm")
    except Exception as e:  # API varies between point releases
        rep.data.setdefault("notes", []).append(f"Netclass audit skipped: {e}")

    rep.add("design_rule_floors", not issues, issues)


def audit_track_widths(board, min_trace, rep):
    issues = []
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            continue
        if t.GetWidth() + EPS_NM < min_trace:
            p = t.GetStart()
            issues.append(
                f"Track net '{t.GetNetname()}' on {board.GetLayerName(t.GetLayer())} "
                f"width {mm(t.GetWidth()):.4f} mm @({mm(p.x):.3f},{mm(p.y):.3f})")
    rep.add("track_width_min", not issues, issues)


def