# AS5600 Magnetic Angle Encoder: Wokwi Simulation for ESP32-S3

This project has five files:

```
├── diagram.json
├── as5600.chip.json
├── as5600.chip.c
├── sketch.ino
└── wokwi.toml        (only needed for VS Code / CLI)
```

The custom chip emulates the AS5600 at I²C address **0x36**. It implements:

- The real register map: ZMCO, ZPOS, MPOS, MANG, CONF, STATUS, RAW ANGLE, ANGLE, AGC, MAGNITUDE and BURN.
- The DIR pin.
- An analog **OUT** pin.
- Interactive sliders for angle, rotation speed and magnet strength.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim",
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
        "speed": "0",
        "magnet": "60"
      }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swDir",
      "top": 180,
      "left": 270,
      "attrs": {}
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VCC", "esp:3V3.1", "red",    [ "h-30", "v-40" ] ],
    [ "enc1:GND", "esp:GND.1", "black",  [ "h-30", "v60" ] ],
    [ "enc1:SDA", "esp:8",     "green",  [ "h-40" ] ],
    [ "enc1:SCL", "esp:9",     "blue",   [ "h-50" ] ],
    [ "enc1:OUT", "esp:4",     "orange", [ "h-60" ] ],
    [ "enc1:PGO", "esp:3V3.1", "red",    [] ],

    [ "enc1:DIR",   "swDir:2",   "violet", [] ],
    [ "swDir:1",    "esp:GND.1", "black",  [] ],
    [ "swDir:3",    "esp:3V3.1", "red",    [] ]
  ],
  "dependencies": {}
}
```

The DIR pin is wired as follows:

- **DIR = GND:** angle increases clockwise.
- **DIR = VCC:** angle increases counter-clockwise.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "AS5600 Sim",
  "pins": [
    "VCC",
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
      "label": "Magnet angle (deg) – used when speed = 0",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "speed",
      "label": "Rotation speed (RPM, ± = direction)",
      "type": "range",
      "min": -300,
      "max": 300,
      "step": 1
    },
    {
      "id": "magnet",
      "label": "Magnet strength (%)",
      "type": "range",
      "min": 0,
      "max": 100,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
#define REG_RAW_H     0x0C
#define REG_RAW_L     0x0D
#define REG_ANGLE_H   0x0E
#define REG_ANGLE_L   0x0F
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

#define STATUS_MH     (1 << 3)
#define STATUS_ML     (1 << 4)
#define STATUS_MD     (1 << 5)

#define TICK_US       10000   // 10 ms physics update

typedef struct {
  pin_t pin_vcc, pin_dir, pin_out, pin_pgo;
  uint32_t attr_angle, attr_speed,