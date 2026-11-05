# AS5600 Magnetic Angle Encoder: Wokwi Simulation on ESP32-S3

This project provides a complete Wokwi setup:

- **`diagram.json`**: wiring for the ESP32-S3 DevKitC-1, the custom AS5600 chip, an LED and a DIR switch.
- **`as5600.chip.json`**: pin and control definitions for the custom chip.
- **`as5600.chip.c`**: register-accurate AS5600 emulation over I2C, plus the analog OUT pin.
- **`sketch.ino`**: an Arduino test firmware for the ESP32-S3.
- **`wokwi.toml`**: configuration for the VS Code / CLI workflow.

---

## 1. Project file layout

```
as5600-esp32s3/
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml            (only needed for VS Code / wokwi-cli)
```

> In the online Wokwi editor, create the chip using **"+" → "Custom chip" → C**. Name it `as5600` so the files become `as5600.chip.json` and `as5600.chip.c`. The editor compiles the chip automatically.

---

## 2. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim – Cycle 13",
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
        "rpm": "0",
        "magnet": "50"
      }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "sw_dir",
      "top": 170,
      "left": 300,
      "attrs": { "value": "0" }
    },
    {
      "type": "wokwi-led",
      "id": "led_md",
      "top": -60,
      "left": 400,
      "attrs": { "color": "green", "label": "MD" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r_led",
      "top": 10,
      "left": 430,
      "rotate": 90,
      "attrs": { "value": "330" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VDD", "red",   [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black", [ "h0" ] ],

    [ "esp:8", "enc1:SDA", "green",  [ "h0" ] ],
    [ "esp:9", "enc1:SCL", "yellow", [ "h0" ] ],

    [ "esp:4", "enc1:OUT", "orange", [ "h0" ] ],

    [ "sw_dir:2", "enc1:DIR",  "violet", [ "v0" ] ],
    [ "sw_dir:1", "esp:GND.2", "black",  [ "v0" ] ],
    [ "sw_dir:3", "esp:3V3.2", "red",    [ "v0" ] ],

    [ "enc1:GPO",   "led_md:A",  "blue",  [ "h0" ] ],
    [ "led_md:C",   "r_led:1",   "black", [ "v0" ] ],
    [ "r_led:2",    "esp:GND.3", "black", [ "v0" ] ]
  ],
  "dependencies": {}
}
```

**Pin mapping:**

| AS5600 pin | ESP32-S3 / part | Purpose |
|---|---|---|
| VDD | 3V3 | Power |
| GND | GND | Ground |
| SDA | GPIO 8 | I2C data (S3 default) |
| SCL | GPIO 9 | I2C clock (S3 default) |
| OUT | GPIO 4 (ADC1_CH3) | Analog angle output |
| DIR | Slide switch | Direction: GND = CW+, VDD = CCW+ |
| GPO | LED | "Magnet detected" indicator (simulation extra) |

---

## 3. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 13",
  "pins": [
    "VDD",
    "OUT",
    "GND",
    "GPO",
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
      "id": "rpm",
      "label": "Auto-rotation (RPM, ±)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 1
    },
    {
      "id": "magnet",
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

## 4. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// I2C address 0x36. Implements the register map, ZPOS/MPOS/MANG scaling,
// STATUS (MD/ML/MH), AGC, MAGNITUDE, DIR pin, the analog OUT pin and BURN.
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define AS5600_ADDR   0x36

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
#define REG_STATUS    0x0B
#define REG_RAW_H     0x0C
#define REG_RAW_L     0x0D
#define REG_ANGLE_H   0x0E
#define REG_ANGLE_L   0x0F
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1