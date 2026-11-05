# AS5600 Magnetic Encoder: Wokwi Custom Chip on ESP32-S3

This project emulates the AS5600 12-bit magnetic rotary encoder as a Wokwi custom chip. It covers:

- The I²C register map at address `0x36`.
- ZPOS, MPOS and MANG scaling.
- The DIR pin.
- STATUS and AGC registers, plus magnitude.
- The analog OUT pin.
- The datasheet's pointer behavior for RAW ANGLE, ANGLE and MAGNITUDE.

Interactive sliders in the simulator let you change the magnet angle and field strength.

I wasn't sure what "Cycle 27" refers to. If it's a project iteration label, the files below are a complete, self-contained set you can drop in.

---

## Project layout

```
├── diagram.json
├── wokwi.toml            (only needed for VS Code / CLI)
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino            (or src/main.cpp for PlatformIO)
```

In the Wokwi web editor, add the chip by pressing **F1 → "Create custom chip" → C**. Then paste the `.chip.json` and `.chip.c` contents below. The web editor compiles the chip automatically.

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
      "top": 30,
      "left": 260,
      "attrs": { "angle": "0", "magnet": "70" }
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
    [ "esp:TX",  "$serialMonitor:RX", "", [] ],
    [ "esp:RX",  "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "blue",   [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "yellow", [ "h0" ] ],
    [ "esp:10",    "enc1:DIR", "orange", [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "green",  [ "h0" ] ],

    [ "r_sda:1", "esp:3V3.1", "red",  [] ],
    [ "r_sda:2", "enc1:SDA",  "blue", [] ],
    [ "r_scl:1", "esp:3V3.1", "red",  [] ],
    [ "r_scl:2", "enc1:SCL",  "yellow", [] ]
  ],
  "dependencies": {}
}
```

Notes on the wiring:

- **I²C bus:** GPIO8 is SDA and GPIO9 is SCL.
- **DIR:** driven by GPIO10. LOW means angle increases clockwise; HIGH reverses the direction.
- **OUT:** goes to GPIO4 (ADC1_CH3), so you can verify the analog output mode.
- **Pull-ups:** Wokwi doesn't actually need them, but they're included for realism.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "AS5600 Sim",
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
      "label": "Magnet angle (deg)",
      "type": "range",
      "min": 0,
      "max": 360,
      "step": 0.1
    },
    {
      "id": "magnet",
      "label": "Field strength (mT)",
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
// AS5600 12-bit magnetic rotary encoder - Wokwi custom chip
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

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

// Field strength window (mT), per datasheet recommendation
#define FIELD_MIN_MT  30.0f
#define FIELD_MAX_MT  90.0f

typedef struct {
  pin_t pin_vcc, pin_gnd, pin_dir, pin_out, pin_pgo;
  uint32_t attr_angle;
  uint32_t attr_magnet;

  uint8_t  reg_ptr;
  bool     first_write;   // next write byte is the register pointer

  // Programmable registers
  uint16_t zpos;          // 12-bit
  uint16_t mpos;          // 12-bit
  uint16_t mang;          // 12-bit
  uint16_t conf;          // 14-bit
  uint8_t  zmco;          // burn counter (0..3)

  // Latched outputs (snapshot taken at I2C start for consistent H/L reads)
  uint16_t raw_angle;
  uint16_t angle;
  uint16_t magnitude;
  uint8_t  agc;
  uint8_t  status;
} chip_state_t;

/* ---------------------------------------------------------------------- */
/*