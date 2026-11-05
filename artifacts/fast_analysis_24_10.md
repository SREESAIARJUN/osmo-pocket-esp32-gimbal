# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip on ESP32-S3

This is a complete, working project with four files:

| File | Purpose |
|---|---|
| `diagram.json` | Wiring: ESP32-S3 DevKitC-1 + custom AS5600 chip |
| `as5600.chip.json` | Chip pin definitions and interactive controls |
| `as5600.chip.c` | Register-level AS5600 emulation (I²C at 0x36) |
| `sketch.ino` | ESP32-S3 test firmware |

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 24",
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
      "attrs": { "angle": "45", "magnet": "50", "rpm": "0" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VCC", "esp:3V3.1", "red",    [ "h0" ] ],
    [ "enc1:GND", "esp:GND.1", "black",  [ "h0" ] ],
    [ "enc1:SDA", "esp:8",     "green",  [ "h0" ] ],
    [ "enc1:SCL", "esp:9",     "blue",   [ "h0" ] ],
    [ "enc1:OUT", "esp:4",     "orange", [ "h0" ] ],
    [ "enc1:DIR", "esp:GND.1", "gray",   [ "h0" ] ],
    [ "enc1:PGO", "esp:3V3.1", "violet", [ "h0" ] ]
  ],
  "dependencies": {}
}
```

**Wiring summary:**
- SDA connects to GPIO8 and SCL to GPIO9.
- The analog OUT pin connects to GPIO4 (ADC1_CH3).
- DIR is tied to GND, so the angle increases with clockwise rotation.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 24",
  "pins": ["VCC", "GND", "SDA", "SCL", "OUT", "DIR", "PGO", ""],
  "controls": [
    { "id": "angle",  "label": "Magnet angle (°)",       "type": "range", "min": 0,    "max": 360, "step": 0.5 },
    { "id": "magnet", "label": "Magn