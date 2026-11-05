# AS5600 Magnetic Angle Encoder: Wokwi Simulation on ESP32-S3

The project has five files:

```
as5600-esp32s3/
├── diagram.json        ← wiring
├── as5600.chip.json    ← custom chip pinout + UI controls
├── as5600.chip.c       ← custom chip logic (I2C register model)
├── sketch.ino          ← ESP32-S3 test firmware
└── wokwi.toml          ← only needed for VS Code / CLI
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 21 – AS5600 sim",
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
      "attrs": { "angle": "45", "rpm": "0", "magnet": "1" }
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

    [ "enc1:VCC", "esp:3V3.1", "red",    [] ],
    [ "enc1:GND", "esp:GND.1", "black",  [] ],
    [ "enc1:SDA", "esp:8",     "green",  [] ],
    [ "enc1:SCL", "esp:9",     "blue",   [] ],
    [ "enc1:DIR", "esp:GND.1", "black",  [] ],
    [ "enc1:OUT", "esp:4",     "orange", [] ],

    [ "r1:1", "esp:3V3.1", "red",   [] ],
    [ "r1:2", "esp:8",     "green", [] ],
    [ "r2:1", "esp:3V3.1", "red",   [] ],
    [ "r2:2", "esp:9",     "blue",  [] ]
  ],
  "dependencies": {}
}
```

**Wiring notes**
- GPIO8 is SDA and GPIO9 is SCL. These are the ESP32-S3 Arduino defaults.
- The 4.7 kΩ pull-ups are optional in Wokwi but mirror real hardware.
- DIR to GND gives clockwise-increasing readings. Wire it to `3V3` to reverse the direction.
- OUT goes to GPIO4, an ADC1 channel, so you can sample the analog output.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 21",
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
      "label": "Magnet angle (deg)",
      "type": "range",
      "min": 0,
      "max": 359,
      "step": 1
    },
    {
      "id": "rpm",
      "label": "Auto-rotate (RPM, ± = direction)",
      "type": "range",
      "min": -120,
      "max": 120,
      "step": 1
    },
    {
      "id": "magnet",
      "label": "Magnet: 0=none 1=ok 2=too strong 3=too weak",
      "type": "range",
      "min": 0,
      "max": 3,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// I2C address 0x36. Implements the register map from the ams datasheet.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define AS5600_ADDR     0x36

// ---- Register map ----
#define REG_ZMCO        0x00
#define REG_ZPOS_H      0x01
#define REG_ZPOS_L      0x02
#define REG_MPOS_H      0x03
#define REG_MPOS_L      0x04
#define REG_MANG_H      0x05
#define REG_MANG_L      0x06
#define REG_CONF_H      0x07
#define REG_CONF_L      0x08
#define REG_STATUS      0x0B
#define REG_RAW_ANGLE_H 0x0C
#define REG_RAW_ANGLE_L 0x0D
#define REG_ANGLE_H     0x0E
#define REG_ANGLE_L     0x0F
#define REG_AGC         0x1A
#define REG_MAGNITUDE_H 0x1B
#define REG_MAGNITUDE_L 0x1C
#define REG_BURN        0xFF

#define STATUS_MH  (1 << 3)   // magnet too strong
#define STATUS_ML  (1 << 4)   // magnet too weak
#define STATUS_MD  (1 << 5)   // magnet detected

#define TICK_US    1000       // 1 ms simulation tick

typedef struct {
  uint8_t  regs[256];
  uint8_t  ptr;               // register address pointer
  bool     expect_addr;       // next written byte is the register pointer

  uint32_t attr_angle;
  uint32_t attr_rpm;
  uint32_t attr_magnet;

  pin_t    pin_dir;
  pin_t    pin_out;
  pin_t    pin_pgo;

  double   spin_deg;          // accumulated auto-rotation
  uint16_t last_out_angle;
} chip_state_t;

// ---------------------------------------------------------------------------
static inline uint16_t get12(chip_state_t *c, uint8_t hi) {
  return ((uint16_t)(c->regs[hi] & 0x0F) << 8) | c->regs[hi + 1];
}
static inline void put12(chip_state_t *c, uint8_t hi, uint16_t v) {
  c->regs[hi]     = (v >> 8) & 0x0F;
  c->regs[hi + 1] = v & 0xFF;
