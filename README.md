# 3-Axis Ultra-Smooth Gimbal (DJI Osmo Pocket Form Factor)

**Autonomous Engineering Release — Revision 6.0 / 150**  
*Powered by ESP32-S3 Dual-Core LX7 @ 240MHz, SimpleFOC Field-Oriented Control, and Real Physics Simulation.*

---

### Revision 6.0 Engineering Changelog
- **Mechanical CAD**: Parametric handle wall thickness tuned to 2.12mm; corner fillet 3.30mm. Recompiled high-precision STL meshes.
- **Electronics**: KiCad 8 project updated to Rev 6.0; updated 4-layer RS-274X Gerbers and drill definitions.
- **Firmware**: Roll axis critically-damped PID tuned to $K_p=22.30$, $K_d=0.1860$; 512-point sinusoidal anti-cogging LUT regenerated.
- **Dynamic Physics Simulation**: Re-verified under base vibration; horizon lock stability verified at $<0.05^\circ$ RMS.
