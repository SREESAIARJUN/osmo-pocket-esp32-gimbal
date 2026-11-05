# AS5600 Magnetic Encoder: Wokwi Custom Chip for ESP32-S3

This project contains five files:

1. `diagram.json` wires the ESP32-S3 to the custom AS5600 chip.
2. `as5600.chip.json` defines the chip's pins and its live controls.
3. `as5600.chip.c` implements the chip logic: the I²C register map, the angle math and the analog output.
4. `sketch.ino` is an Arduino test program for the ESP32-S3.
5. `wokwi.toml` is only needed if you run the simulation in VS Code.

The I²C address is **0x36**. SDA is on **GPIO8** and SCL is on **GPIO9**.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 15",
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
      "attrs": { "angle": "0", "rpm": "30", "magnet": "1" }
    },
    { "type": "wokwi-resistor", "id": "r1", "top": -60, "left": 180, "rotate": 90, "attrs": { "value": "4700" } },
    { "type": "wokwi-resistor", "id": "r2", "top": -60, "left": 210, "rotate": 90, "attrs": { "value": "4700" } },
    { "type": "wokwi-led", "id": "led1", "top": 160, "left": 400, "attrs": { "color": "green" } }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",   [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black", [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "green", [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "blue",  [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "orange",[ "h0" ] ],
    [ "esp:5",     "enc1:DIR", "purple",[ "h0" ] ],
    [ "enc1:PGO",  "esp:3V3.2", "red",  [ "h0" ] ],

    [ "r1:1", "esp:3V3.2", "red",   [] ],
    [ "r1:2", "esp:8",     "green", [] ],
    [ "r2:1", "esp:3V3.2", "red",   [] ],
    [ "r2:2", "esp:9",     "blue",  [] ],

    [ "esp:6",      "led1:A", "green", [] ],
    [ "led1:C",     "esp:GND.2", "black", [] ]
  ],
  "dependencies": {}
}
```

How the pins are used:

- **GPIO4** (ADC1_CH3) reads the analog `OUT` pin.
- **GPIO5** drives `DIR`. LOW means the angle increases clockwise; HIGH means it increases counter-clockwise.
- **GPIO6** lights the LED whenever a magnet is detected (status bit MD).

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 15",
  "pins": ["VCC", "OUT", "GND", "PGO", "SDA", "SCL", "DIR", ""],
  "controls": [
    { "id": "angle",  "label": "Magnet angle (deg)", "type": "range", "min": 0, "max": 359, "step": 1 },
    { "id": "rpm",    "label": "Auto-spin (RPM, 0=off)", "type": "range", "min": 0, "max": 600, "step": 1 },
    { "id": "magnet", "label": "Field: 0=weak 1=ok 2=strong 3=none", "type": "range", "min": 0, "max": 3, "step": 1 }
  ]
}
```

The pin order follows the real SOIC-8 package: VDD5V, OUT, GND, PGO, SDA, SCL, DIR, with the last slot left empty.

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder – Wokwi custom chip
// SPDX-License-Identifier: MIT
#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AS5600_ADDR   0x36

/* Register map */
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

#define STATUS_MH     (1 << 3)   // magnet too strong
#define STATUS_ML     (1 << 4)   // magnet too weak
#define STATUS_MD     (1 << 5)   // magnet detected

typedef struct {
  pin_t pin_out, pin_dir, pin_pgo;
  uint32_t attr_angle, attr_rpm, attr_magnet;

  uint8_t  reg_ptr;
  bool     expect_ptr;      // first byte of a write transaction = register pointer

  uint8_t  zmco;
  uint16_t zpos, mpos, mang, conf;

  double   phys_angle;      // mechanical angle in degrees [0,360)
  int      last_manual_deg; // tracks slider changes
  uint16_t raw, scaled;
  uint8_t  status, agc;
  uint16_t magnitude;
} chip_state_t;

/* ---------- helpers ---------- */

static void update_magnet(chip_state_t *c) {
  int m = (int)attr_read(c->attr_magnet);
