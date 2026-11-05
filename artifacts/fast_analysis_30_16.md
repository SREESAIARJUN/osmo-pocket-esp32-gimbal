# KiCad 8 PCB Verification Script: Netlist Continuity and 0.1 mm Trace/Space

`verify_pcb.py` is a standalone script that reads the `.kicad_pcb` file directly. It does not depend on `pcbnew`. It can also cross-check the board against the schematic netlist and run KiCad's own DRC engine as a second, independent check.

**What it checks:**

| Check | Description |
|---|---|
| **Trace width** | Every track segment and arc must be ≥ 0.1 mm wide. Zone minimum thickness is also checked. |
| **Copper clearance** | Copper items on different nets that share a layer must be ≥ 0.1 mm apart. This covers tracks, arcs, vias and pads. |
| **Net continuity** | Each net must form a single connected copper island. Connections through tracks, vias, pads and filled zones all count. Any net that is split is reported as open. |
| **Netlist parity** | Optional (`--netlist`). Each pad's net on the board is compared with the schematic `.net` export. |
| **KiCad DRC** | Optional (`--kicad-cli`). Runs `kicad-cli pcb drc` and adds its errors to the result. |

The script exits with code `0` if all checks pass and `1` if any fail, so you can use it as a CI gate.

```python
#!/us