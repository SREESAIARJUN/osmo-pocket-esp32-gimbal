# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip on ESP32-S3 (Cycle 12)

This project simulates an AS5600 on the I²C bus of an ESP32-S3. It includes the full register map, start/stop position scaling, the DIR pin, magnet status, AGC and magnitude, plus an analog OUT pin. A slider sets the shaft angle, and an optional RPM control makes the shaft spin continuously.

## Project layout

```
├── diagram.json
├── wokwi.toml            (only needed for VS Code / wokwi-cli)
├── sketch.ino            (or src/main.cpp for PlatformIO)
├── as5600.chip.json
└── as5600.chip.c
```

In the Wokwi web editor, use **"+" → New file**. Name the files `as5600.chip.json` and `as5600.chip.c`, and the editor compiles the chip automatically.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 12 – AS5600 Encoder",
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
      "top": 20,
      "left": 260,
      "attrs": { "angle": "0", "magnet": "50", "rpm": "0" }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swdir",
      "top": 150,
      "left": 280,
      "attrs": {}
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VDD",   "red",    [ "v0" ] ],
    [ "esp:GND.1", "enc1:GND",   "black",  [ "v0" ] ],
    [ "esp:8",     "enc1:SDA",   "green",  [ "h0" ] ],
    [ "esp:9",     "enc1:SCL",   "blue",   [ "h0" ] ],
    [ "esp:1",     "enc1:OUT",   "orange", [ "h0" ] ],
    [ "esp:2",     "enc1:PGO",   "gray",   [ "h0" ] ],

    [ "swdir:2",   "enc1:DIR",   "purple", [ "v0" ] ],
    [ "swdir:1",   "esp:GND.1",  "black",  [ "v0" ] ],
    [ "swdir:3",   "esp:3V3.1",  "red",    [ "v0" ] ]
  ],
  "dependencies": {}
}
```

**Wiring summary**

| AS5600 pin | Connects to | Purpose |
|---|---|---|
| SDA | GPIO8 | Default I²C SDA on S3 |
| SCL | GPIO9 | Default I²C SCL on S3 |
| OUT | GPIO1 | ADC1_CH0, reads the analog output |
| DIR | Slide switch | GND = clockwise (CW) increases angle, VDD = counter-clockwise (CCW) increases |
| PGO | GPIO2 | Present but unused |

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 12",
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
      "label": "Shaft angle (°)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "magnet",
      "label": "Magnet strength (%)",
      "type": "range",
      "min": 0,
      "max": 100,
      "step": 1
    },
    {
      "id": "rpm",
      "label": "Auto-spin (RPM)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// I2C address 0x36. Cycle 12.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define AS5600_ADDR   0x36

// Register map
#define REG_ZMCO      0x00
#define REG_ZPOS_H    0x01
#define REG_ZPOS_L    0x02
#define