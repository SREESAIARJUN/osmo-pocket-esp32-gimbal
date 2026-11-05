# AS5600 Magnetic Angle Encoder on ESP32-S3: Wokwi Simulation (Cycle 11)

This project simulates an AS5600 magnetic angle encoder in Wokwi. A custom chip exposes the AS5600 register map over I2C at address **0x36**. Two sliders in the simulator control the angle and the magnetic field strength. The chip also drives the analog **OUT** pin, which the ESP32-S3 reads on an ADC pin.

## File layout

```
project/
├── diagram.json
├── wokwi.toml            (VS Code / CLI only)
├── sketch.ino            (or src/main.cpp)
├── as5600.chip.json
└── as5600.chip.c
```

On wokwi.com, use **Add file → Custom chip (C)** and name the chip `as5600`. Wokwi then creates `as5600.chip.json` and `as5600.chip.c`. Replace their contents with the files below.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 11 – AS5600 I2C encoder",
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
      "id": "rSDA",
      "top": -60,
      "left": 180,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "rSCL",
      "top": -60,
      "left": 210,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [] ],
    [ "esp:GND.1", "enc1:GND", "black",  [] ],
    [ "esp:8",     "enc1:SDA", "green",  [] ],
    [ "esp:9",     "enc1:SCL", "blue",   [] ],
    [ "esp:GND.1", "enc1:DIR", "black",  [] ],
    [ "esp:1",     "enc1:OUT", "orange", [] ],

    [ "rSDA:1", "esp:3V3.1", "red",   [] ],
    [ "rSDA:2", "enc1:SDA",  "green", [] ],
    [ "rSCL:1", "esp:3V3.1", "red",   [] ],
    [ "rSCL:2", "enc1:SCL",  "blue",  [] ]
  ],
  "dependencies": {}
}
```

**Pin wiring summary**

| Signal | Connects to |
|---|---|
| SDA | GPIO8 (default SDA on the S3 DevKitC) |
| SCL | GPIO9 (default SCL on the S3 DevKitC) |
| OUT | GPIO1 (ADC1_CH0) |
| DIR | GND (clockwise rotation increases the angle) |

The pull-up resistors are optional in Wokwi but match real hardware.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 11",
  "pins": ["VCC", "GND", "SDA", "SCL", "DIR", "OUT", "PGO"],
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
      "id": "field",
      "label": "Field strength (mT)",
      "type": "range",
      "min": 0,
      "max": 150,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// I2C address 0x36. Implements the register map, ZPOS/MPOS/MANG scaling,
// STATUS (MD/ML/MH), AGC, MAGNITUDE, DIR pin and analog OUT.
#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AS5600_ADDR   0x36

// Register map
#define R_ZMCO        0x00
#define R_ZPOS_H      0x01
#define R_ZPOS_L      0x02
#define R_MPOS_H      0x03
#define R_MPOS_L      0x04
#define R_MANG_H      0x05
#define R_MANG_L      0x06
#define R_CONF_H      0x07
#define R_CONF_L      0x08
#define R_STATUS      0x0B
#define R_RAW_H       0x0C
#define R_RAW_L       0x0D
#define R_ANG_H       0x0E
#define R_ANG_L       0x0F
#define R_AGC         0x1A
#define R_MAG_H       0x1B
#define R_MAG_L       0x1C
#define R_BURN        0xFF

#define ST_MH  (1 << 3)   // magnet too strong
#define ST_ML  (1 << 4)   // magnet too weak
#define ST_MD  (