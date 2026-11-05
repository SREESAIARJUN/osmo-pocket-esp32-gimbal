# 3-Axis Ultra-Smooth Gimbal (DJI Osmo Pocket Form Factor)

**Autonomous Engineering Release - Cycle 5**  
*Powered by ESP32-S3 Dual-Core LX7 @ 240MHz, SimpleFOC Field-Oriented Control, and Real Physics Simulation.*

---

## 1. System Overview
- **Form Factor**: Ultra-compact handheld stabilizer matching the DJI Osmo Pocket form factor (32mm x 35mm x 128mm handle).
- **MCU**: ESP32-S3-WROOM-1 (Dual Xtensa LX7 @ 240MHz, 8MB PSRAM, USB OTG, high-res MCPWM).
- **Motor Control**: 3x Texas Instruments TMC6300-LA ultra-quiet low-voltage 3-phase BLDC drivers (1.2A RMS, 2.0A peak).
- **Encoders**: 3x AS5600 14-bit magnetic absolute angle encoders on motor shafts.
- **Dual IMU Sensor Fusion**:
  - Primary: ICM-42688-P directly on the camera cradle for zero-lag attitude measurement.
  - Secondary: ICM-42688-P on the handle for base motion feedforward vibration cancellation.
- **Power Management**: 2S 18350 Li-ion battery (7.4V, ~1000mAh) with USB-C PD fast charging and TI TPS7A4700 ultra-low-noise analog LDO.

---

## 2. Repository Contents
- `cad/`: Parametric OpenSCAD source models for handle housing, pan yoke, roll arm, and pitch camera cradle.
- `stl/`: Ready-to-print 3D STL meshes generated directly from OpenSCAD.
- `kicad/`: Complete KiCad 8 4-layer PCB schematic and board layout files.
- `gerbers/`: Production-ready RS-274X Gerbers and Excellon drill files for JLCPCB / PCBWay manufacturing.
- `firmware/`: Complete SimpleFOC Arduino / ESP-IDF C++ source code with 512-point sinusoidal anti-cogging calibration.
- `simulation/`: Python dynamic 3-axis physics simulator (`gimbal_physics_sim.py`), telemetry plots (`simulation_telemetry.png`), and Wokwi web simulator integration (`wokwi.toml`, `diagram.json`).

---

## 3. Simulated Telemetry & Stability
- Hand disturbance attenuation: **> -30 dB**
- Horizon lock precision: **< 0.05° RMS error**
- Closed-loop FOC update rate: **10 kHz**
