# AS5600 Magnetic Encoder: Wokwi Simulation for ESP32-S3 (Cycle 8)

This is a complete Wokwi project:
- An AS5600 custom chip (`as5600.chip.json` and `as5600.chip.c`) with a register-level I²C model.
- The `diagram.json` wiring.
- An Arduino test sketch.

## Project Layout

```
├── diagram.json
├── wokwi.toml            (only for VS Code / CLI)
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 8",
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
      "attrs": { "angle": "0", "magnitude": "2000" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r_sda",
      "top": -60,
      "left": 180,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r_scl",
      "top": -60,
      "left": 210,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "v0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "v0" ] ],
    [ "esp:8",     "enc1:SDA", "blue",   [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "yellow", [ "h0" ] ],
    [ "esp:GND.1", "enc1:DIR", "gray",   [ "v0" ] ],
    [ "esp:4",     "enc1:OUT", "green",  [ "h0" ] ],

    [ "r_sda:1", "esp:3V3.1", "red",  [] ],
    [ "r_sda:2", "enc1:SDA",  "blue", [] ],
    [ "r_scl:1", "esp:3V3.1", "red",  [] ],
    [ "r_scl:2", "enc1:SCL",  "yellow", [] ]
  ],
  "dependencies": {}
}
```

Wiring notes:
- **I²C pins:** GPIO8 is SDA and GPIO9 is SCL. These are the ESP32-S3 `Wire` defaults.
- **Pull-up resistors:** Wokwi's I²C bus does not need them. They are included for schematic realism.
- **DIR pin:** tied to GND, which gives clockwise-increasing counts. Rewire it to `3V3` to reverse the direction.
- **OUT pin:** analog output (10–90 % of VDD), read on GPIO4 (ADC1_CH3).

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 8",
  "pins": [
    "VCC",
    "OUT",
    "GND",
    "",
    "",
    "SDA",
    "SCL",
    "DIR"
  ],
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
      "id": "magnitude",
      "label": "Field magnitude (0=no magnet)",
      "type": "range",
      "min": 0,
      "max": 4095,
      "step": 1
    }
  ]
}
```

The pin order follows the real SOIC-8 package: 1 VDD5V, 2 VDD3V3 (unused), 3 OUT, 4 GND, 5 PGO, 6 SDA, 7 SCL, 8 DIR. It is simplified here to the pins that matter.

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder – Wokwi custom chip
// Register-accurate I2C model (addr 0x36) + analog OUT + DIR pin
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define REG_RAW_ANG_H   0x0C
#define REG_RAW_ANG_L   0x0D
#define REG_ANGLE_H     0x0E
#define REG_ANGLE_L     0x0F
#define REG_AGC         0x1A
#define REG_MAG_H       0x1B
#define REG_MAG_L       0x1C
#define REG_BURN        0xFF

// STATUS bits
#define STATUS_MH       0x08   // magnet too strong
#define STATUS_ML       0x10   // magnet too weak
#define STATUS_MD       0x20   // magnet detected

typedef struct {
  // pins
  pin_t pin_vcc, pin_gnd, pin_dir, pin_out;

  // attributes (UI controls)
  uint32_t attr_angle;
  uint32_t attr_magnitude;

  // I2C state
  uint8_t  reg_ptr;
  bool     first_write;

  // writable / OTP registers
  uint8_t  zmco;
  uint16_t zpos;
  uint16_t mpos;
  uint16_t mang;
  uint16_t conf;

  // live values
  uint16_t raw_angle;
  uint16_t angle;
  uint16_t magnitude;
  uint8_t  agc;
  uint8_t  status;
} chip_state_t;

// ---------------------------------------------------------------------------
// Forward declarations
static bool    on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool    on_i2c_write(void *user_data, uint8_t data);
static void    on_i2c_disconnect(void *user_data);
static void    on