# AS5600 Magnetic Encoder: Wokwi Custom Chip for ESP32-S3 (Cycle 3)

This setup simulates the AS5600 at I²C address `0x36` with the following features:

- **Registers:** the full register map, including the AS5600's special pointer behavior for the angle and magnitude register pairs.
- **Angle scaling:** ZPOS, MPOS and MANG start/stop scaling.
- **DIR pin:** reverses the counting direction.
- **Magnet status:** the MD, ML and MH bits, plus AGC and MAGNITUDE values driven by a "magnet strength" slider.
- **OUT pin:** analog output (full or reduced range) or PWM output, selected by the CONF.OUTS bits.
- **BURN command:** emulates one-time-programmable (OTP) burning and increments the ZMCO counter.

## Project layout

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only needed for the VS Code / CLI workflow)
```

On **wokwi.com**, add the two chip files with *"+ New file"* and give them exactly these names. Wokwi compiles `*.chip.c` automatically.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 3",
  "editor": "wokwi",
  "parts": [
    {
      "type": "board-esp32-s3-devkitc-1",
      "id": "esp",
      "top": 0,
      "left": 0,
      "attrs": {}
    },
    {
      "type": "chip-as5600",
      "id": "enc1",
      "top": 30,
      "left": 230,
      "attrs": { "angle": "45", "magnet": "60"