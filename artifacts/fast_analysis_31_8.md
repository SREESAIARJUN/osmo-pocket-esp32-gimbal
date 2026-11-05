# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3

This project contains five files:

1. `diagram.json` wires the ESP32-S3 DevKitC-1 to a custom AS5600 chip.
2. `as5600.chip.json` defines the chip's pins and its interactive controls (angle, magnet strength, spin speed).
3. `as5600.chip.c` implements the I²C register map, the ZPOS/MPOS/MANG scaling, the DIR pin, the status/AGC logic and the OUT pin (analog or PWM).
4. `sketch.ino` is an ESP32-S3 Arduino test sketch.
5. `wokwi.toml` is only needed for the VS Code extension.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 31",
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
      "left": 260,
      "attrs": {
        "angle": "90",
        "magnitude": "2000",
        "rpm": "0"
      }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": -60,
      "left": 200,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": -60,
      "left": 230,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "green",  [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "blue",   [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "orange", [ "h0" ] ],
    [ "esp:GND.2", "enc1:DIR", "gray",   [ "h0" ] ],

    [ "r1:1", "esp:3V3.1", "red",   [] ],
    [ "r1:2", "enc1:SDA",  "green", [] ],
    [ "r2:1", "esp:3V3.1", "red",   [] ],
    [ "r2:2", "enc1:SCL",  "blue",  [] ]
  ],
  "dependencies": {}
}
```

Wiring notes:
- **DIR** is tied to GND, so the angle increases clockwise. Move the DIR wire to 3V3 to reverse the direction.
- **GPIO8/GPIO9** are the default Arduino SDA/SCL pins on the ESP32-S3.
- **The 4.7 kΩ pull-ups** are optional, because Wokwi does not require them. They are included for realism.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "AS5600 Sim – Cycle 31",
  "pins": [
    "VCC",
    "GND",
    "SDA",
    "SCL",
    "OUT",
    "DIR",
    "PGO",
    ""
  ],
  "controls": [
    {
      "id": "angle",
      "label": "Magnet angle (deg)",
      "type": "range",
      "min": 0,
      "max": 359,
      "step": 1
    },
    {
      "id": "magnitude",
      "label": "Magnet strength (0-4095)",
      "type": "range",
      "min": 0,
      "max": 4095,
      "step": 1
    },
    {
      "id": "rpm",
      "label": "Auto-spin (RPM, 0 = off)",
      "type": "range",
      "min": -300,
      "max": 300,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// I2C address 0x36. Implements the register map, ZPOS/MPOS/MANG scaling,
// DIR pin, STATUS/AGC/MAGNITUDE, and OUT (analog full/reduced range, PWM).
// SPD