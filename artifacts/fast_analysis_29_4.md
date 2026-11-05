# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3

This project simulates an AS5600 encoder on an ESP32-S3 in Wokwi. It includes the wiring diagram, a custom chip written in C, and an Arduino test sketch.

**What the chip does:**
- Responds on I2C address `0x36`.
- Implements the AS5600 register map, including RAW ANGLE, ANGLE, STATUS, AGC, MAGNITUDE, the ZPOS/MPOS/MANG range settings and BURN.
- Lets you set the magnet angle and magnet strength with live sliders.
- Reverses direction when the DIR pin is pulled high.
- Drives the OUT pin with an analog voltage proportional to the angle.

## Project Layout

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only needed for VS Code / wokwi-cli)
```

> If you use the Wokwi web editor, add the two `as5600.chip.*` files with **"New file"**. The editor compiles the chip automatically.

---

## 1. `diagram.json`

The ESP32-S3 uses GPIO 8 for SDA and GPIO 9 for SCL. A slide switch connects DIR to either GND or 3V3.

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
      "attrs": { "angle": "90", "magnitude": "2000" }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swdir",
      "top": 170,
      "left": 280,
      "attrs": {}
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
    [ "enc1:DIR", "swdir:2",   "purple", [ "v0" ] ],
    [ "swdir:1",  "esp:GND.1", "black",  [ "v0" ] ],
    [ "swdir:3",  "esp:3V3.1", "red",    [ "v0" ] ]
  ],
  "dependencies": {}
}
```

---

## 2. `as5600.chip.json`

This file declares the chip's pins and the two slider controls.

```json
{
  "name": "AS5600",
  "author": "Wokwi Custom Chip",
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
      "label": "Magnet angle (°)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "magnitude",
      "label": "Magnet strength (0-4095)",
      "type": "range",
      "min": 0,
      "max": 4095,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor - Wokwi custom chip
// I2C address 0x36. Register behaviour follows the ams AS5600 datasheet.

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
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
#define REG_RAW_H     0x0C
#define REG_RAW_L     0x0D
#define REG_ANGLE_H   0x0E
#define REG_ANGLE_L   0x0F
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

// STATUS bits
#define STATUS_MH     0x08   // magnet too strong
#define STATUS_ML     0x10   // magnet too weak
#define STATUS_MD     0x20   // magnet detected

typedef struct {
  pin_t pin_out;
  pin_t pin_dir;
  pin_t pin_pgo;
  uint32_t attr_angle;
  uint32_t attr_mag;
  uint8_t regs[256];
  uint8_t ptr;
  bool    expect_addr;     // next written byte is the register pointer
  uint16_t raw_angle;
  uint16_t angle;
} chip_state_t;

static bool    on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool    on_i2c_write(void *user_data, uint8_t data);
static void    on_i2c_disconnect(void *user_data);
static void    on_timer(void *user_data);

static uint16_t get12(chip_state_t *c, uint8_t hi) {
  return ((c->regs[hi] & 0x0F) << 8) | c->regs[hi + 1];
}

static void set12(chip_state_t *c, uint8_t hi, uint16_t v) {
  c->regs[hi]     = (v >> 8) & 0x0F;
  c->regs[hi + 1] = v & 0xFF;
}

// Recompute RAW ANGLE, ANGLE, STATUS, AGC and MAGNITUDE from the sliders + DIR pin
static void update_sensor(chip_state_t *c) {
  float deg = attr_read_float(