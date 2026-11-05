# AS5600 Magnetic Angle Encoder: Wokwi Simulation on ESP32-S3 (Cycle 4)

The project has five files:

```
project/
├── diagram.json
├── sketch.ino              (or src/main.cpp)
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml              (VS Code / CLI only)
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 4 – AS5600 I2C Encoder",
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
      "attrs": { "angle": "0" }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swDir",
      "top": 170,
      "left": 270,
      "attrs": {}
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "green",  [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "blue",   [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "orange", [ "h0" ] ],

    [ "swDir:2",   "enc1:DIR", "violet", [ "v0" ] ],
    [ "swDir:1",   "esp:GND.2", "black", [ "v0" ] ],
    [ "swDir:3",   "esp:3V3.2", "red",   [ "v0" ] ]
  ],
  "dependencies": {}
}
```

**Wiring summary:**

| AS5600 pin | ESP32-S3 pin | Purpose |
|---|---|---|
| SDA | GPIO 8 | I2C data |
| SCL | GPIO 9 | I2C clock |
| OUT | GPIO 4 | Analog output (ADC1_CH3) |
| DIR | Slide switch | GND = clockwise, 3V3 = counter-clockwise |

Wokwi's I2C bus does not need pull-up resistors.

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 4",
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
      "label": "Magnet angle (°)",
      "type": "range",
      "min": 0,
      "max": 359.9,
      "step": 0.1
    },
    {
      "id": "magnitude",
      "label": "Field strength (0=none, 1=ok, 2=strong)",
      "type": "range",
      "min": 0,
      "max": 2,
      "step": 1
    }
  ]
}
```

---

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder – Wokwi custom chip
// I2C address 0x36. Implements the register map, ZPOS/MPOS/MANG scaling,
// DIR pin, STATUS/AGC/MAGNITUDE, and the analog OUT pin.

#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

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

#define STATUS_MH  (1 << 3)   // magnet too strong
#define STATUS_ML  (1 << 4)   // magnet too weak
#define STATUS_MD  (1 << 5)   // magnet detected

typedef struct {
  pin_t pin_dir;
  pin_t pin_out;
  pin_t pin_vcc;

  uint32_t attr_angle;
  uint32_t attr_magnitude;

  uint8_t  reg_ptr;
  bool     first_byte;   // next write byte is the register address

  // Writable configuration
  uint8_t  zmco;
  uint16_t zpos;
  uint16_t mpos;
  uint16_t mang;
  uint16_t conf;

  // Latched 16-bit value so high/low bytes are read consistently
  uint16_t latch;
} chip_state_t;

/* ---------- Angle computation ---------- */

static uint16_t raw_angle(chip_state_t *c) {
  float deg = attr_read_float(c->attr_angle);
  if (deg < 0) deg = 0;
  if (deg >= 360.0f) deg = 359.999f;
  uint16_t raw = (uint16_t)(deg * 4096.0f / 360.0f) & 0x0FFF;
  // DIR high = counter-clockwise increases the value
  if (pin_read(c->pin_dir) == HIGH) raw = (4096 - raw) & 0x0FFF;
  return raw;
}

static uint16_t scaled_angle(chip_state_t *c) {
  uint16_t raw = raw_angle(c);
  uint16_t start = c->zpos & 0x0FFF;
  uint32_t range;

  if (c->m