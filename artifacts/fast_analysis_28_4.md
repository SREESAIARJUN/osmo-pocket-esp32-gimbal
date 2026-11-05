# AS5600 Magnetic Encoder: Wokwi Custom Chip for ESP32-S3

This project has five files:

- `diagram.json`
- `as5600.chip.json`
- `as5600.chip.c`
- `sketch.ino`
- `wokwi.toml` (only needed for the VS Code extension)

You can paste them into a Wokwi ESP32-S3 project. To add the chip files in the browser, use **Add file → Custom chip (C)** and name the chip `as5600`. The diagram uses ESP32-S3 GPIO 8 for SDA, GPIO 9 for SCL and GPIO 4 for the analog OUT pin. I wasn't sure what "Cycle 28" refers to, so it isn't reflected in these files.

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
      "top": 20,
      "left": 260,
      "attrs": { "angle": "90", "field": "60" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],
    [ "enc1:VCC", "esp:3V3.1", "red",    [] ],
    [ "enc1:GND", "esp:GND.1", "black",  [] ],
    [ "enc1:SDA", "esp:8",     "green",  [] ],
    [ "enc1:SCL", "esp:9",     "blue",   [] ],
    [ "enc1:OUT", "esp:4",     "orange", [] ],
    [ "enc1:DIR", "esp:GND.2", "black",  [] ]
  ],
  "dependencies": {}
}
```

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "AS5600 Sim",
  "pins": ["VCC", "GND", "SDA", "SCL", "DIR", "OUT", "PGO", ""],
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
      "id": "field",
      "label": "Magnetic field (mT)",
      "type": "range",
      "min": 0,
      "max": 120,
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
// the DIR pin, STATUS/AGC/MAGNITUDE and the analog OUT pin.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define AS5600_ADDR 0x36

// Register addresses
#define REG_ZMCO      0x00
#define REG_ZPOS_H    0x01
#define REG_ZPOS_L    0x02
#define REG_MPOS_H    0x03
#define REG_MPOS_L    0x04
#define REG_MANG_H    0x05
#define REG_MANG_L    0x06
#define REG_CONF_H    0x07
#define REG_CONF_L    0x08
#define REG_RAW_H     0x0C
#define REG_RAW_L     0x0D
#define REG_ANGLE_H   0x0E
#define REG_ANGLE_L   0x0F
#define REG_STATUS    0x0B
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

typedef struct {
  pin_t pin_dir;
  pin_t pin_out;
  pin_t pin_vcc;
  uint32_t attr_angle;
  uint32_t attr_field;

  // Programmable registers
  uint8_t  zmco;
  uint16_t zpos, mpos, mang, conf;

  // I2C state
  uint8_t reg_ptr;
  bool    first_write;   // next written byte is the register pointer
  uint16_t latch;        // snapshot for coherent 16-bit reads
} chip_state_t;

// ---------- Model ----------
static uint16_t get_raw_angle(chip_state_t *c) {
  float deg = attr_read_float(c->attr_angle);
  int raw = (int)((deg / 360.0f) * 4096.0f + 0.5f) & 0x0FFF;
  // DIR low (GND) = clockwise increases; DIR high = reversed
  if (pin_read(c->pin_dir) == HIGH) raw = (4096 - raw) & 0x0FFF;
  return (uint16_t)raw;
}

static uint16_t get_scaled_angle(chip_state_t *c) {
  uint16_t raw = get_raw_angle(c);
  uint32_t range;
  if (c->mpos != 0)      range = (c->mpos - c->zpos) & 0x0FFF;
  else if (c->mang != 0) range = c->mang & 0x0FFF;
  else                   range = 4096;
  if (range == 0) range = 4096;

  uint32_t rel = (raw - c->zpos) & 0x0FFF;
  if (range < 4096 && rel >= range) {
    // Outside the programmed range: saturate to the nearest end
    uint32_t over = rel - range;
    return (over < (4096 - range) / 2) ? 4095 : 0;
  }
  uint32_t a = (rel * 4096) / range;
  return a > 4095 ? 4095 : (uint16_t)a;
}

static float field_mT(chip_state_t *c) { return attr_read_float(c->attr_field); }

static uint8_t get_status(chip