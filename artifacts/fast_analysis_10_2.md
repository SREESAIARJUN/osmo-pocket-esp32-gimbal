# AS5600 Magnetic Angle Encoder: Wokwi Simulation on ESP32-S3 (Cycle 10)

This Wokwi project simulates an AS5600 magnetic angle encoder as a custom chip connected to an ESP32-S3 over I2C. The custom chip models the following:

- **I2C slave** at address `0x36` with the real register map: ZMCO, ZPOS, MPOS, MANG, CONF, STATUS, RAW ANGLE, ANGLE, AGC, MAGNITUDE and BURN.
- **Register pointer behavior:**
  - The pointer auto-increments after each byte.
  - For RAW ANGLE, ANGLE and MAGNITUDE, reading the low byte wraps the pointer back to the high byte, as the datasheet describes for these registers.
- **ZPOS/MPOS/MANG angle scaling** to produce the ANGLE output.
- **DIR pin:** connecting it to VDD reverses the counting direction.
- **OUT pin:** an analog voltage proportional to the angle, between 10% and 90% of VDD.
- **Interactive sliders** for the shaft angle and the magnetic field strength. These drive the STATUS bits MD, ML and MH, plus AGC and MAGNITUDE.

---

## Project file layout

```
├── diagram.json
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino
```

In Wokwi web, add the two chip files with **"New file"**. Wokwi compiles `*.chip.c` automatically. The part type `chip-as5600` maps to `as5600.chip.json`.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 10 - AS5600 on ESP32-S3",
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
      "attrs": { "angle": "0", "field": "60" }
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

    [ "esp:3V3.1", "enc1:VDD",  "red",    [] ],
    [ "esp:GND.1", "enc1:GND",  "black",  [] ],

    [ "esp:8",     "enc1:SDA",  "green",  [] ],
    [ "esp:9",     "enc1:SCL",  "blue",   [] ],

    [ "esp:4",     "enc1:OUT",  "orange", [] ],
    [ "esp:5",     "enc1:DIR",  "purple", [] ],
    [ "esp:GND.1", "enc1:PGO",  "black",  [] ],

    [ "r_sda:1",   "esp:3V3.1", "red",    [] ],
    [ "r_sda:2",   "enc1:SDA",  "green",  [] ],
    [ "r_scl:1",   "esp:3V3.1", "red",    [] ],
    [ "r_scl:2",   "enc1:SCL",  "blue",   [] ]
  ],
  "dependencies": {}
}
```

Pin assignments on the ESP32-S3:

| ESP32-S3 GPIO | AS5600 pin | Purpose |
|---|---|---|
| 8 | SDA | I2C data (Arduino-ESP32 default for the S3) |
| 9 | SCL | I2C clock (Arduino-ESP32 default for the S3) |
| 4 | OUT | Analog angle output, read on ADC1 |
| 5 | DIR | Driven by firmware to set the counting direction |

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 10",
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
      "label": "Shaft angle (deg)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "field",
      "label": "Magnet field strength (%)",
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
// SPDX-License-Identifier: MIT
#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define AS5600_ADDR 0x36

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

#define BURN_ANGLE   0x80
#define BURN_SETTING 0x40

#define STATUS_MH (1 << 3)  // magnet too strong
#define STATUS_ML (1 << 4)  // magnet too weak
#define STATUS_MD (1 << 5)  // magnet detected

typedef struct {
  pin_t pin_vdd, pin_out, pin_dir, pin_pgo;
  uint32_t attr_angle, attr_field;

  uint8_t  reg_ptr;
  bool     first_write_byte;

  // Non-volatile / config registers
  uint8_t  zmco;
  uint16_t zpos, mpos, mang, conf;

  // Snapshot latched at start of a read transaction
  uint16_t raw_angle, angle, magnitude;
  uint8_t  status, agc;

  timer_t  out_timer;
} chip_state_t;

static bool    on_i2c_