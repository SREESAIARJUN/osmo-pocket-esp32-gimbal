# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3

This project emulates the AS5600 over I²C at address `0x36`. It covers:

- The full register map.
- ZPOS/MPOS/MANG scaling.
- The DIR pin.
- Magnet status and AGC flags.
- An analog OUT pin.
- Interactive sliders to control the magnet.

## Project layout

```
├── diagram.json
├── wokwi.toml          (only for VS Code / CLI; wokwi.com doesn't need it)
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino          (or src/main.cpp for PlatformIO)
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim - Cycle 18",
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
      "attrs": { "angle": "90", "magnet": "1", "strength": "50" }
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
    [ "enc1:DIR", "esp:GND.2", "gray",   [ "h0" ] ]
  ],
  "dependencies": {}
}
```

The pin choices are:

| AS5600 pin | ESP32-S3 pin | Purpose |
|---|---|---|
| SDA | GPIO8 | I²C data |
| SCL | GPIO9 | I²C clock |
| OUT | GPIO4 | ADC1_CH3 input for the analog output |
| DIR | GND | Clockwise rotation increases the angle |

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 18",
  "pins": ["VCC", "GND", "SDA", "SCL", "DIR", "OUT", "PGO", ""],
  "controls": [
    { "id": "angle",    "label": "Magnet angle (°)",       "type": "range", "min": 0, "max": 359.9, "step": 0.1 },
    { "id": "magnet",   "label": "Magnet present (0/1)",   "type": "range", "min": 0, "max": 1,     "step": 1 },
    { "id": "strength", "label": "Field strength (mT)",    "type": "range", "min": 0, "max": 120,   "step": 1 }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor - Wokwi custom chip
// SPDX-License-Identifier: MIT
#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define AS5600_ADDR   0x36

// Register map
#define R_ZMCO        0x00
#define R_ZPOS_H      0x01
#define R_ZPOS_L      0x02
#define R_MPOS_H      0x03
#define R_MPOS_L      0x04
#define R_MANG_H      0x05
#define R_MANG_L      0x06
#define R_CONF_H      0x07
#define R_CONF_L      0x08
#define R_STATUS      0x0B
#define R_RAW_H       0x0C
#define R_RAW_L       0x0D
#define R_ANGLE_H     0x0E
#define R_ANGLE_L     0x0F
#define R_AGC         0x1A
#define R_MAG_H       0x1B
#define R_MAG_L       0x1C
#define R_BURN        0xFF

#define ST_MH  (1 << 3)   // magnet too strong
#define ST_ML  (1 << 4)   // magnet too weak
#define ST_MD  (1 << 5)   // magnet detected

typedef struct {
  pin_t vcc, gnd, dir, out, pgo;
  uint32_t attr_angle, attr_magnet, attr_strength;

  uint8_t  regs[256];
  uint8_t  ptr;
  bool     expect_addr;
  uint16_t latch;          // keeps H/L byte pair coherent within a burst
  bool     latch_valid;
} chip_t;

/* ---------- helpers ---------- */

static uint16_t reg16(chip_t *c, uint8_t h) {
  return ((uint16_t)(c->regs[h] & 0x0F) << 8) | c->regs[h + 1];
}

static uint16_t raw_angle(chip_t *c) {
  float deg = attr_read_float(c->attr_angle);
  while (deg < 0)      deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  uint16_t raw = (uint16_t)(deg * 4096.0f / 360.0f) & 0x0FFF;
  // DIR high => counter-clockwise increases angle (invert)
  if (pin_read(c->dir) == HIGH) raw = (4096 - raw) & 0x0FFF;
  return raw;
}

static uint16_t scaled_angle(chip_t *c) {
  uint16_t raw  = raw_angle(c);
  uint16_t zpos = reg16(c, R_ZPOS_H);
  uint16_t mpos = reg16(c, R_MPOS_H);
  uint16_t mang = reg16(c, R_M