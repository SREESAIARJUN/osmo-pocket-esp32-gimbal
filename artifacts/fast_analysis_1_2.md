# AS5600 Magnetic Angle Encoder: Wokwi Simulation for ESP32-S3 (Cycle 1)

This first cycle delivers a working I²C model of the AS5600 that an ESP32-S3 can read through `Wire`. It includes the core register map and live angle controls.

## Project Structure

```
as5600-esp32s3/
├── diagram.json
├── wokwi.toml               (only for VS Code / wokwi-cli)
├── sketch.ino
├── as5600.chip.json
└── as5600.chip.c
```

On **wokwi.com**, add the two chip files with the "+" button, then choose "New file…". The names must be exactly `as5600.chip.json` and `as5600.chip.c`. The `chip-as5600` part in the diagram will then resolve automatically.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 1",
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
      "attrs": { "angle": "45", "rpm": "0", "magnet": "1" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r_sda",
      "top": -60,
      "left": 160,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r_scl",
      "top": -60,
      "left": 185,
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
    [ "esp:GND.1", "enc1:DIR", "gray",   [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "orange", [ "h0" ] ],

    [ "r_sda:1", "esp:3V3.1", "red",   [] ],
    [ "r_sda:2", "esp:8",     "green", [] ],
    [ "r_scl:1", "esp:3V3.1", "red",   [] ],
    [ "r_scl:2", "esp:9",     "blue",  [] ]
  ],
  "dependencies": {}
}
```

The pull-up resistors are optional in Wokwi, since the I²C bus is idealized. They are included so the wiring matches real hardware. The DIR pin is tied to GND, which gives clockwise as increasing angle.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 1",
  "pins": [
    "VCC",
    "GND",
    "SDA",
    "SCL",
    "DIR",
    "OUT",
    "PGO"
  ],
  "controls": [
    {
      "id": "angle",
      "label": "Magnet angle (°)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "rpm",
      "label": "Auto-spin (RPM, ± = direction)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 1
    },
    {
      "id": "magnet",
      "label": "Magnet: 0=none 1=OK 2=weak 3=strong",
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
// Cycle 1: I2C register map, RAW ANGLE / ANGLE with ZPOS/MPOS/MANG scaling,
//          STATUS / AGC / MAGNITUDE, DIR pin, analog OUT, auto-spin.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define AS5600_ADDR    0x36

// Register map (datasheet AS5600 v1-06)
#define REG_ZMCO       0x00
#define REG_ZPOS_H     0x01
#define REG_ZPOS_L     0x02
#define REG_MPOS_H     0x03
#define REG_MPOS_L     0x04
#define REG_MANG_H     0x05
#define REG_MANG_L     0x06
#define REG_CONF_H     0x07
#define REG_CONF_L     0x08
#define REG_STATUS     0x0B
#define REG_RAW_H      0x0C
#define REG_RAW_L      0x0D
#define REG_ANGLE_H    0x0E
#define REG_ANGLE_L    0x0F
#define REG_AGC        0x1A
#define REG_MAG_H      0x1B
#define REG_MAG_L      0x1C
#define REG_BURN       0xFF

#define STATUS_MH      (1 << 3)   // magnet too strong
#define STATUS_ML      (1 << 4)   // magnet too weak
#define STATUS_MD      (1 << 5)   // magnet detected

#define TICK_US        1000       // 1 ms update for auto-spin

typedef struct {
  pin_t pin_vcc, pin_gnd, pin_dir, pin_out, pin_pgo;
  i2c_dev_t i2c;

  uint32_t attr_angle;
  uint32_t attr_rpm;
  uint32_t attr_magnet;

  uint8_t  regs[256];
  uint8_t  ptr;              // register address pointer
  bool     expect_addr;      // next written byte is the register address

  double   spin_offset_deg;  // accumulated auto-spin rotation
  timer_t  timer;
} chip_state_t;

/* ---------- helpers ---------- */

static uint16_t reg16(chip_state_t *c, uint8_t hi) {
  return (uint16_t)(((c->regs[hi] & 0x0F) << 8) | c->regs[hi + 1]);
}

static void set_reg16(chip_state_t *c, uint8_t hi, uint16_t v) {
  c->regs[hi]     = (v >> 8) & 0x0F;
  c->regs[hi + 1] = v & 0xFF;
}

static bool is_writable(uint8_t reg) {
  return (reg >= REG_ZPOS_H && reg <= REG_CONF_L) || reg == REG_BURN;
}

// Recomp