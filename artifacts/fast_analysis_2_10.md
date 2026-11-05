# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3 (Cycle 2)

This is a complete Wokwi project. A custom chip emulates the AS5600 at I2C address `0x36` and connects to an ESP32-S3.

**Features**
- The angle register map follows the datasheet: `ZMCO`, `ZPOS`, `MPOS`, `MANG`, `CONF`, `STATUS`, `RAW ANGLE`, `ANGLE`, `AGC`, `MAGNITUDE` and `BURN`.
- When you read the high byte, the 12-bit value is latched, so high and low bytes always match.
- The address pointer follows the AS5600 rule: it does not auto-increment past the low byte of `RAW ANGLE`, `ANGLE` or `MAGNITUDE`.
- `ZPOS`, `MPOS` and `MANG` scale the output angle.
- The `DIR` pin reverses the direction.
- Live sliders set the angle, magnet strength and auto-spin RPM.
- The analog `OUT` pin is driven through the DAC.

---

## 1. Project structure

```
├── diagram.json
├── wokwi.toml            (only needed for VS Code / wokwi-cli)
├── sketch.ino
├── as5600.chip.json
└── as5600.chip.c
```

On wokwi.com, add the two `as5600.*` files with **"New file…"**. Wokwi compiles the chip automatically when the part type is `chip-as5600`.

---

## 2. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 2",
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
      "top": 40,
      "left": 260,
      "attrs": { "angle": "45", "magnet": "60", "rpm": "0" }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swDir",
      "top": 200,
      "left": 300,
      "attrs": {}
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VDD3V3", "esp:3V3.1", "red",    [ "h0" ] ],
    [ "enc1:VDD5V",  "esp:3V3.1", "red",    [ "h0" ] ],
    [ "enc1:GND",    "esp:GND.1", "black",  [ "h0" ] ],
    [ "enc1:SDA",    "esp:8",     "green",  [ "h0" ] ],
    [ "enc1:SCL",    "esp:9",     "yellow", [ "h0" ] ],
    [ "enc1:OUT",    "esp:4",     "orange", [ "h0" ] ],

    [ "swDir:2", "enc1:DIR",  "blue",  [ "v0" ] ],
    [ "swDir:1", "esp:GND.1", "black", [ "v0" ] ],
    [ "swDir:3", "esp:3V3.1", "red",