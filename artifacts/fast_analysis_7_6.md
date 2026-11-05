# AS5600 Magnetic Encoder: Wokwi Custom Chip for ESP32-S3 (Cycle 7)

This is a complete Wokwi project. A custom chip emulates the AS5600 at I2C address `0x36`. The simulated device provides:

- The full register map, including 12-bit RAW ANGLE and the scaled ANGLE.
- ZPOS, MPOS and MANG range scaling.
- Magnet detection status bits and an AGC value.
- MSB-read latching.
- The DIR pin, which reverses the count direction.
- An analog OUT pin you can read with the ESP32-S3 ADC.

Interactive sliders let you set the angle, magnet strength and continuous spin RPM.

---

## 1. Project layout

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only needed for VS Code / CLI)
```

On **wokwi.com**, add the two chip files as follows:

1. Press the blue **+** in the editor.
2. Choose **"Custom chip (C)"** and name it `as5600`.
3. Paste in the two files below.

---

## 2. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 7 - AS5600 I2C Encoder",
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
      "attrs": { "angle": "45", "magnet": "60", "rpm": "0" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": -60,
      "left": 160,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": -60,
      "left": 190,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swDir",
      "top": 170,
      "left": 240,
      "attrs": {}
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red", [ "v0" ] ],
    [ "esp:GND.1", "enc1:GND", "black", [ "v0" ] ],

    [ "esp:8", "enc1:SDA", "green", [ "h0" ] ],
    [ "esp:9", "enc1:SCL", "blue", [ "h0" ] ],

    [ "r1:1", "esp:3V3.1", "red", [ "v0" ] ],
    [ "r1:2", "esp:8", "green", [ "v0" ] ],
    [ "r2:1", "esp:3V3.1", "red", [ "v0" ] ],
    [ "r2:2", "esp:9", "blue", [ "v0" ] ],

    [ "enc1:OUT", "esp:4", "orange", [ "h0" ] ],
    [ "enc1:PGO", "esp:3V3.1", "gray", [ "h0" ] ],

    [ "swDir:2", "enc1:DIR", "purple", [ "v0" ] ],
    [ "swDir:1", "esp:GND.1", "black", [ "v0" ] ],
    [ "swDir:3", "esp:3V3.1", "red", [ "v0" ] ]
  ],
  "dependencies": {}
}
```

**Pin map (ESP32-S3)**

| AS5600 | ESP32-S3 | Note |
|---|---|---|
| VCC | 3V3 | |
| GND | GND | |
| SDA | GPIO 8 | 4.7 kΩ pull-up |
| SCL | GPIO 9 | 4.7 kΩ pull-up |
| OUT | GPIO 4 (ADC1_CH3) | Analog angle output |
| DIR | Slide switch | GND gives CW+, 3V3 gives CCW+ |
| PGO | 3V3 | Unused |

---

## 3. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 7",
  "pins": [
    "VCC",
    "GND",
    "SDA",
    "SCL",
    "DIR",
    "OUT",
    "PGO",
    ""
  ],
  "controls": [
    {
      "id": "angle",
      "label": "Magnet angle (deg)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      