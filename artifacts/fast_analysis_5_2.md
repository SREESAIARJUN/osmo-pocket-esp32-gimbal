# AS5600 Magnetic Encoder: Wokwi Custom Chip on ESP32-S3

This setup emulates the AS5600 (I²C address `0x36`) register map. It supports:

- RAW ANGLE, ANGLE, STATUS, AGC and MAGNITUDE registers
- ZPOS/MPOS/MANG range scaling and CONF writes
- The DIR pin, which reverses direction
- An analog OUT pin
- The register auto-increment and wrap behavior described in the datasheet
- Coherent 16-bit reads
- Interactive sliders for angle, auto-spin RPM and magnet strength

## Project layout

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only for VS Code / CLI)
```

On wokwi.com, add the chip with **"+" → Custom chip (C)**, name it `as5600`, and paste the two chip files. Wokwi compiles them automatically.

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
      "attrs": { "angle": "45", "rpm": "0", "magnet": "50" }
    },
    { "type": "wokwi-resistor", "id": "r1", "top": -60, "left": 200, "rotate": 90, "attrs": { "value": "4700" } },
    { "type": "wokwi-resistor", "id": "r2", "top": -60, "left": 230, "rotate": 90, "attrs": { "value": "4700" } },
    {
      "type": "wokwi-slide-switch",
      "id": "swDir",
      "top": 170,
      "left": 300,
      "attrs": {}
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
    [ "enc1:PGO", "esp:3V3.1", "red",    [] ],

    [ "r1:1", "esp:3V3.1", "red",   [] ],
    [ "r1:2", "esp:8",     "green", [] ],
    [ "r2:1", "esp:3V3.1", "red",   [] ],
    [ "r2:2", "esp:9",     "blue",  [] ],

    [ "swDir:2", "enc1:DIR",  "purple", [] ],
    [ "swDir:1", "esp:GND.1", "black",  [] ],
    [ "swDir:3", "esp:3V3.1", "red",    [] ]
  ],
  "dependencies": {}
}
```

Wiring summary:

- **I²C:** SDA → GPIO8, SCL → GPIO9. These are the ESP32-S3 Arduino defaults.
- **Analog output:** OUT → GPIO4 (ADC1_CH3).
- **DIR:** a slide switch connects DIR to GND (clockwise) or 3V3 (counter-clockwise).

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Wokwi Sim",
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
      "max": 359,
      "step": 1
    },
    {
      "id": "rpm",
      "label": "Auto-spin (RPM, ±)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 10
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

The pin order matches the real SOIC-8 pinout: VDD5V, VDD3V3/OUT, GND, PGO, SDA, SCL, DIR.

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary position sensor – Wokwi custom chip
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define AS5600_ADDR      0x36

/* Register map */
#define REG_ZMCO         0x00
#define REG_ZPOS_H       0x01
#define REG_ZPOS_L       0x02
#define REG_MPOS_H       0x03
#define REG_MPOS_L       0x04
#define REG_MANG_H       0x05
#define REG_MANG_L       0x06
#define REG_CONF_H       0x07
#define REG_CONF_L       0x08
#define REG_STATUS       0x0B
#define REG_RAW_ANGLE_H  0x0C
#define REG_RAW_ANGLE_L  0x0D
#define REG_ANGLE_H      0x0E
#define REG_ANGLE_L      0x0F
#define REG_AGC          0x1A
#define REG_MAGNITUDE_H  0x1B
#define REG_MAGNITUDE_L  0x1C
#define REG_BURN         0xFF

#define STATUS_MH  (1 << 3)   /* magnet too strong */
#define STATUS_ML  (1 << 4)   /* magnet too weak   */
#define STATUS_MD  (1 << 5)   /* magnet detected   */

#define TICK_US    1000       /* physics update period: 1 ms */

typedef struct