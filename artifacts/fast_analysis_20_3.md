# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip on ESP32-S3

This project contains five files:

| File | Purpose |
|---|---|
| `diagram.json` | Wiring for the ESP32-S3 and the custom chip |
| `as5600.chip.json` | Chip pinout and interactive controls |
| `as5600.chip.c` | Chip logic: I2C slave, register map, rotation model, analog output |
| `sketch.ino` | ESP32-S3 test firmware |
| `libraries.txt` | Library list (empty) |

On wokwi.com, create an **ESP32-S3** project. Add the chip files using **"+" → "Custom chip (C)"**, name the chip `as5600`, and replace the generated contents with the files below.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim - Cycle 20",
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
        "angle": "0",
        "rpm": "30",
        "magnet": "1"
      }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": -60,
      "left": 180,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": -60,
      "left": 210,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VDD", "red",    [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "blue",   [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "yellow", [ "h0" ] ],
    [ "esp:GND.1", "enc1:DIR", "black",  [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "green",  [ "h0" ] ],
    [ "esp:5",     "enc1:PGO", "gray",   [ "h0" ] ],

    [ "r1:1", "esp:3V3.1", "red",    [] ],
    [ "r1:2", "esp:8",     "blue",   [] ],
    [ "r2:1", "esp:3V3.1", "red",    [] ],
    [ "r2:2", "esp:9",     "yellow", [] ]
  ],
  "dependencies": {}
}
```

**Wiring summary:**

| AS5600 pin | ESP32-S3 pin | Notes |
|---|---|---|
| SDA | GPIO 8 | Default I2C SDA on the S3 |
| SCL | GPIO 9 | Default I2C SCL on the S3 |
| OUT | GPIO 4 | ADC1_CH3, reads the analog angle |
| DIR | GND | Clockwise = increasing angle |
| PGO | GPIO 5 | Unused input |

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 20",
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
      "label": "Magnet angle (deg) [used when RPM = 0]",
      "type": "range",
      "min": 0,
      "max": 360,
      "step": 1
    },
    {
      "id": "rpm",
      "label": "Rotation speed (RPM, negative = CCW)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 1
    },
    {
      "id": "magnet",
      "label": "Magnet: 0=none 1=OK 2=too weak 3=too strong",
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
// AS5600 12-bit magnetic rotary position sensor - Wokwi custom chip
// I2C address 0x36. Implements the register map, the address-pointer
// behaviour of the datasheet, ZPOS/MPOS/MANG scaling, the DIR pin,
// the analog OUT pin and STATUS/AGC/MAGNITUDE emulation.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define REG_ANGLE_H   0x0E
#define REG_ANGLE_L   0x0F
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

// STATUS bits
#define STATUS_MH     (1 << 3)   // magnet too strong
#define STATUS_ML     (1 << 4)   // magnet too weak
#define STATUS_MD     (1 << 5)   // magnet detected

#define TICK_US       1000       // 1 ms physics tick

typedef struct {
  // pins
  pin_t pin_out;
  pin_t pin_dir;
  pin_t pin_pgo;
  pin_t pin_vdd;

  // attributes (UI controls)
  uint32_t attr_angle;
  uint32_t attr_rpm;
  uint32_t attr_magnet;

  // I2C state
  uint8_t  reg_ptr;
  bool     first_byte;      // next write byte is the register address

  // register storage (programmable)
  uint8_t  zmco;
  uint16_t zpos;
  uint16_t mpos;
  uint16_t mang;
  uint16_t conf;

  // simulated physics
  double   mech_angle_deg;  // 0..360
  uint16_t raw_angle;       // 0..4095
  uint16_t scaled_angle;    // 0..4095
  uint8_t  status;
  uint8_t  agc;
  uint16_t magnitude;

  // latched 16-bit values for coherent multi-byte reads
  uint16_t latch;
} chip_state_t;

static bool    on_i2c_connect(void *user_data, uint32_t address, bool connect);
static uint