# AS5600 Magnetic Encoder Custom Chip on ESP32-S3 (Wokwi)

This project simulates the AS5600 12-bit magnetic angle encoder as a Wokwi custom chip. The ESP32-S3 reads it over I²C at address `0x36`. You set the magnet angle and field strength with sliders in the simulator.

## Project Structure

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only needed for VS Code / CLI)
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Simulation - Cycle 14",
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
      "attrs": { "angle": "0", "magnitude": "60" }
    },
    {
      "type": "wokwi-led",
      "id": "led1",
      "top": 180,
      "left": 260,
      "attrs": { "color": "yellow", "label": "PGO" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "blue",   [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "yellow", [ "h0" ] ],
    [ "esp:GND.1", "enc1:DIR", "black",  [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "green",  [ "h0" ] ],

    [ "enc1:PGO",  "led1:C",   "gray",   [ "h0" ] ],
    [ "led1:A",    "esp:3V3.1","red",    [ "h0" ] ]
  ],
  "dependencies": {}
}
```

**Pin mapping:**

| AS5600 pin | ESP32-S3 pin | Purpose |
|---|---|---|
| SDA | GPIO 8 | I²C data (Arduino default for S3) |
| SCL | GPIO 9 | I²C clock |
| OUT | GPIO 4 | Analog output, read by ADC1 |
| DIR | GND | Clockwise = increasing angle |
| VCC / GND | 3V3 / GND | Power |

Wokwi models the I²C bus as ideally pulled up, so no external resistors are needed.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 14",
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
      "label": "Magnet angle (°)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "magnitude",
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
// AS5600 12-bit magnetic rotary position sensor - Wokwi custom chip
// I2C address 0x36. Implements the register map, ZPOS/MPOS/MANG scaling,
// the DIR pin, magnet status bits (MD/ML/MH), AGC, and an analog OUT pin.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define AS5600_ADDR     0x36

// Register map
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

#define STATUS_MH       (1 << 3)   // magnet too strong
#define STATUS_ML       (1 << 4)   // magnet too weak
#define STATUS_MD       (1 << 5)   // magnet detected

typedef struct {
  pin_t pin_vcc, pin_gnd, pin_sda, pin_scl, pin_dir, pin_out, pin_pgo;
  uint32_t attr_angle;
  uint32_t attr_magnitude;

  uint8_t regs[256];
  uint8_t reg_ptr;
  bool    first_write_byte;   // next written byte sets the register pointer
  uint8_t burn_count;         // ZMCO (0..3)
} chip_state_t;

/* ---------- helpers ---------- */

static uint16_t reg16(chip_state_t *c, uint8_t hi) {
  return ((uint16_t)(c->regs[hi] & 0x0F) << 8) | c->regs[hi + 1];
}

static void set_reg16(chip_state_t *c, uint8_t hi, uint16_t v) {
  c->regs[hi]     = (v >> 8) & 0x0F;
  c->regs[hi + 1] = v & 0xFF;
}

// Recompute all read-only registers from the slider values and pins
static void update_sensor(chip_state_t *c) {
  float deg = attr_read_float(c->attr_angle);
  float mag = attr_read_float(c->attr_magnitude);   // 0..100 %

  // DIR pin: low = clockwise increasing, high = reversed
  if (pin_read(c->pin_dir) == HIGH) {
    deg = 360.0f - deg;
  }
  uint16_t raw = (uint16_t)((deg / 360.0f) * 4096.0f) & 0x0FFF;
  set_reg16(c, REG_RAW_ANGLE_H, raw);

  // Scaled ANGLE using ZPOS / MPOS / MANG
  uint16_t zpos = reg16(c, REG_ZPOS_H);
  uint16_t mpos = reg16(c, REG_MPOS_H);
  uint16_t m