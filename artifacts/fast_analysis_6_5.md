# AS5600 Magnetic Angle Encoder: Wokwi Simulation for ESP32-S3 (Cycle 6)

This is a complete, drop-in Wokwi project. It includes:

- An AS5600 custom chip with a register-accurate I²C map.
- ZPOS/MPOS/MANG angle scaling.
- STATUS, AGC and MAGNITUDE registers.
- DIR pin handling.
- An analog OUT pin.
- Live UI controls for angle, auto-spin RPM and magnet strength.

## Project layout

```
├── diagram.json
├── wokwi.toml
├── sketch.ino
├── as5600.chip.json
└── as5600.chip.c
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim - Cycle 6",
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
        "angle": "45",
        "rpm": "0",
        "magnet": "2"
      }
    },
    {
      "type": "wokwi-led",
      "id": "ledMag",
      "top": 160,
      "left": 300,
      "attrs": { "color": "red", "label": "MAG ERR" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": 220,
      "left": 290,
      "attrs": { "value": "220" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VDD", "red", [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black", [ "h0" ] ],
    [ "esp:8", "enc1:SDA", "green", [ "h0" ] ],
    [ "esp:9", "enc1:SCL", "blue", [ "h0" ] ],
    [ "esp:GND.1", "enc1:DIR", "black", [ "h0" ] ],
    [ "esp:4", "enc1:OUT", "orange", [ "h0" ] ],

    [ "esp:5", "ledMag:A", "red", [ "h0" ] ],
    [ "ledMag:C", "r1:1", "black", [ "v0" ] ],
    [ "r1:2", "esp:GND.2", "black", [ "v0" ] ]
  ],
  "dependencies": {}
}
```

> **DIR pin:** DIR is tied to GND, so the angle increases clockwise. Wire it to `esp:3V3.1` (or to a GPIO) to reverse the direction.

---

## 2. `wokwi.toml`

```toml
[wokwi]
version = 1
firmware = "build/sketch.ino.bin"
elf = "build/sketch.ino.elf"

[[chip]]
name = "as5600"
binary = "chips/as5600.chip.wasm"
```

> On **wokwi.com**, the chip files are compiled automatically, so you can skip this file. In **VS Code**, compile the chip to WASM first with `wokwi-cli chip compile` or the Wokwi chip template.

---

## 3. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 6",
  "pins": ["VDD", "OUT", "GND", "PGO", "SDA", "SCL", "DIR", ""],
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
      "id": "rpm",
      "label": "Auto-spin (RPM, ±)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 1
    },
    {
      "id": "magnet",
      "label": "Magnet: 0=none 1=weak 2=ok 3=strong",
      "type": "range",
      "min": 0,
      "max": 3,
      "step": 1
    }
  ]
}
```

---

## 4. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder - Wokwi custom chip
// I2C address 0x36. Cycle 6.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

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
#define REG_ANG_H     0x0E
#define REG_ANG_L     0x0F
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

#define STATUS_MH     (1 << 3)   // magnet too strong
#define STATUS_ML     (1 << 4)   // magnet too weak
#define STATUS_MD     (1 << 5)   // magnet detected

#define TICK_US       1000       // 1 ms physics/update tick

typedef struct {
  pin_t pin_out, pin_dir, pin_pgo;
  uint32_t attr_angle, attr_rpm, attr_magnet;

  uint8_t  regs[256];
  uint8_t  ptr;            // register address pointer
  bool     first_byte;     // next write byte is the register address

  double   mech_deg;       // current magnet angle (deg)
  float    last_ui_angle;  // to detect slider moves
  uint16_t raw;            // 12-bit raw angle
  uint16_t angle;          // 12-bit scaled angle
  uint8_t  zmco;           // burn counter (0..3)
} chip_state_t;

/* ---------- helpers ---------- */

static uint16_t reg16(chip_state_t *s, uint8_t hi) {
  return ((uint16_t)(s->regs[hi] & 0x0F) << 8) | s->regs[hi + 1];
}

static void set16(chip_state_t *s, uint8_t hi, uint16_t v) {
  s->regs[hi]     = (v >> 8) & 0x0F;
  s->regs[hi + 1] = v & 0xFF;
}

static bool is_writable(uint8_t r) {
  return (r >= REG_ZPOS_H && r <= REG_CONF_L) || r == REG_BURN;
}

static void recompute(chip_state_t *s) {
  // ---- Magnet status / AGC / magnitude ----
  int mag = (int)attr_