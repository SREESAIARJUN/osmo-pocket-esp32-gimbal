# AS5600 Magnetic Angle Encoder: Wokwi Simulation on ESP32-S3

This project uses a Wokwi **custom chip** written in C to emulate the AS5600 at I²C address `0x36`. The emulation covers:

- **Register map:** RAW ANGLE, ANGLE, STATUS, AGC, MAGNITUDE, ZPOS, MPOS, MANG, CONF, and BURN.
- **Angle output:** both the raw angle and the scaled angle, which honors the ZPOS/MPOS/MANG range settings.
- **DIR pin:** controls the rotation direction.
- **Analog OUT pin:** supports full-range and reduced-range (10–90 %) modes.
- **Magnet status bits:** MD, ML, and MH, driven by a simulated field strength.
- **Interactive sliders:** angle, field strength, and noise.

## Project Layout

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
└── as5600.chip.c
```

In the Wokwi web editor, add the chip by clicking **"+" → "Custom chip" → C**. Name it `as5600`, then paste the two chip files below.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Simulation - Cycle 22",
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
      "attrs": { "angle": "45", "field": "60", "noise": "0" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VDD", "esp:3V3.1", "red",    [ "v0" ] ],
    [ "enc1:GND", "esp:GND.1", "black",  [ "v0" ] ],
    [ "enc1:SDA", "esp:8",     "green",  [ "h0" ] ],
    [ "enc1:SCL", "esp:9",     "blue",   [ "h0" ] ],
    [ "enc1:OUT", "esp:4",     "orange", [ "h0" ] ],
    [ "enc1:DIR", "esp:5",     "purple", [ "h0" ] ]
  ],
  "dependencies": {}
}
```

The ESP32-S3 drives DIR from GPIO5, so the sketch can flip the rotation direction at runtime. To fix the direction instead, wire DIR to GND for clockwise.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 22",
  "pins": [
    "VDD",
    "OUT",
    "GND",
    "PGO",
    "SDA",
    "SCL",
    "DIR",
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
      "id": "field",
      "label": "Field strength (mT)",
      "type": "range",
      "min": 0,
      "max": 120,
      "step": 1
    },
    {
      "id": "noise",
      "label": "Noise (LSB)",
      "type": "range",
      "min": 0,
      "max": 8,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor - Wokwi custom chip
// I2C address 0x36. SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define AS5600_ADDR   0x36

// Register map
#define REG_ZMCO      0x00
#define REG_ZPOS_H    0x01
#define REG_ZPOS_L    0x02
#define REG_MPOS_H    0x03
#define REG_MPOS_L    0x04
#define REG_MANG_H    0x05
#define REG_MANG_L    0x06
#define REG_CONF_H    0x07
#define REG_CONF_L    0x08
#define REG_STATUS    0x0B
#define