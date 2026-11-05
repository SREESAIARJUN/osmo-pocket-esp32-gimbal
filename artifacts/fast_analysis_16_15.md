# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space DRC (Cycle 16)

The script below runs on its own. It reads the `.kicad_pcb` file directly and checks the board geometry itself, without using KiCad's DRC engine. If `kicad-cli` is installed, it also runs KiCad's DRC and compares the two results.

## What it checks

| # | Check | Method |
|---|-------|--------|
| 1 | **Minimum trace width** ≥ 0.100 mm | Every `segment` and `arc` on copper layers |
| 2 | **Minimum clearance** ≥ 0.100 mm | Exact copper-to-copper distance between items on different nets, per layer. Covers pads, tracks, arcs, vias and filled zones. |
| 3 | **Net continuity** | Builds a connectivity graph per net across layers (vias and through-hole pads join layers). It flags nets whose pads fall into more than one connected island, and reports dangling copper. |
| 4 | **Schematic parity** (optional) | Compares pad nets on the PCB against an exported `.net` netlist |
| 5 | **Rule configuration** | Checks that the `.kicad_pro` rules and netclasses are not looser than 0.1 mm. If they were,