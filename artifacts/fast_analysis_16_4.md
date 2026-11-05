# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3

I wasn't sure what "Cycle 16" refers to, so this is a standalone, working project. Tell me what earlier cycles defined (for example, pin choices or a register subset) and I'll adapt it.

The project has five files:

```
├── diagram.json
├── wokwi.toml          (VS Code / CLI only)
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino
```

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
      "attrs": { "angle": "90", "magnet": "50", "rpm": "0" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": 140,
      "left": 230,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": 170,
      "left": 230,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VCC", "esp:3V3.1", "red",    [] ],
    [ "enc1:GND", "esp:GND.1", "black",  [] ],
    [ "enc1:SDA", "esp:8",     "green",  [] ],
    [ "enc1:SCL", "esp:9",     "blue",   [] ],
    [ "enc1:DIR", "esp:GND.1", "black",  [] ],
    [ "enc1:OUT", "esp:4",     "orange", [] ],

    [ "r1:1", "esp:3V3.1", "red",   [] ],
    [ "r1:2", "esp:8",     "green", [] ],
    [ "r2:1", "esp:3V3.1", "red",   [] ],
    [ "r2:2", "esp:9",     "blue",  [] ]
  ],
  "dependencies": {}
}
```

Wiring notes:

- **I2C:** SDA is GPIO8 and SCL is GPIO9, the ESP32-S3 defaults. The pull-ups are optional in Wokwi but match real hardware.
- **DIR:** tied to GND, so the angle increases clockwise. Tie it to 3V3 to reverse the direction.
- **OUT:** analog output (0–3.3 V proportional to angle), read by the ADC on GPIO4.

---

## 2. `wokwi.toml` (VS Code extension / wokwi-cli only)

```toml
[wokwi]
version = 1
firmware = ".pio/build/esp32-s3/firmware.bin"
elf      = ".pio/build/esp32-s3/firmware.elf"

[[chip]]
name   = "as5600"
binary = "chips/as5600.chip.wasm"
```

On wokwi.com you don't need this file. Instead, add `as5600.chip.json` and `as5600.chip.c` with **"New file…"** (the `.chip.` naming is what tells Wokwi to compile them).

---

## 3. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Wokwi Custom Chip",
  "pins": ["VCC", "GND", "DIR", "SCL", "SDA", "OUT", "", ""],
  "controls": [
    { "id": "angle",  "label": "Magnet angle (deg)",    "type": "range", "min": 0,    "max": 359.9, "step": 0.1 },
    { "id": "magnet", "label": "Field strength (%)",    "type": "range", "min": 0,    "max": 100,   "step": 1 },
    { "id": "rpm",    "label": "Auto-spin (RPM, ±)",    "type": "range", "min": -120, "max": 120,   "step": 1 }
  ]
}
```

---

## 4. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder — Wokwi custom chip
// I2C address 0x36. Implements ZMCO, ZPOS, MPOS, MANG, CONF, RAW ANGLE,
// ANGLE, STATUS, AGC, MAGNITUDE and BURN (simulated, no persistence).
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
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
#define REG_ANG_H     0x0E
#define REG_ANG_L     0x0F
#define REG_AGC       0x1A
#define REG_MAG_H     0x1B
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

#define STATUS_MH  (1 << 3)   // magnet too strong
#define STATUS_ML  (1 << 4)   // magnet too weak
#define STATUS_MD  (1 << 5)   // magnet detected

#define TICK_US 10000         // 10 ms update tick

typedef struct {
  pin_t pin_dir;
  pin_t pin_out;
  uint32_t attr_angle;
  uint32_t attr_magnet;
  uint32_t attr_rpm;
  timer_t timer;

  // I2C state
  uint8_t reg_ptr;
  bool    expect_