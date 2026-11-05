# AS5600 Magnetic Encoder on ESP32-S3 in Wokwi (Cycle 9)

This is a complete Wokwi project. It contains a custom AS5600 chip with a full I2C register map, a `diagram.json` that wires it to an ESP32-S3, and a test sketch.

## Project Layout

```
├── diagram.json
├── wokwi.toml              (only needed for VS Code / CLI)
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino  (or src/main.cpp)
```

On wokwi.com, add `as5600.chip.json` and `as5600.chip.c` through **"+" → New file**. The web editor compiles the chip automatically.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 9",
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
      "attrs": {
        "angle": "90",
        "field": "60",
        "rpm": "0"
      }
    },
    {
      "type": "wokwi-potentiometer",
      "id": "potDummy",
      "top": 200,
      "left": 230,
      "attrs": {}
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
    [ "esp:GND.2", "enc1:DIR", "gray",   [ "h0" ] ]
  ],
  "dependencies": {}
}
```

> The potentiometer is optional decoration. Delete it if you want a minimal diagram.

**Wiring summary**

| Connection | Purpose |
|---|---|
| GPIO8 → SDA, GPIO9 → SCL | Default I2C pins on the ESP32-S3 |
| GPIO4 ← OUT | ADC1 channel, reads the analog output |
| DIR → GND | Clockwise = increasing angle |

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 9",
  "pins": ["VCC", "GND", "SDA", "SCL", "DIR", "OUT", "PGO", ""],
  "controls": [
    { "id": "angle", "label": "Magnet angle (°)",       "type": "range", "min": 0,    "max": 360, "step": 1 },
    { "id": "field", "label": "Field strength (mT)",    "type": "range", "min": 0,    "max": 120, "step": 1 },
    { "id": "rpm",   "label": "Auto-rotate speed (RPM)", "type": "range", "min": -120, "max": 120, "step": 1 }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder – Wokwi custom chip
// Cycle 9 – I2C register map, ZPOS/MPOS/MANG scaling, status flags,
// AGC/MAGNITUDE, DIR pin, analog OUT (full / reduced range).
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define AS5600_ADDR   0x36

// Register addresses
#define REG_ZMCO      0x00
#define