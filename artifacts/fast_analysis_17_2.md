# AS5600 Magnetic Angle Encoder on ESP32-S3: Wokwi Simulation

This setup uses a custom Wokwi chip that emulates the AS5600 register map over I2C at address `0x36`. You can drive the magnet angle, field strength and spin speed from sliders while the simulation runs.

## Project Structure

```
├── diagram.json
├── wokwi.toml
├── as5600.chip.json
├── as5600.chip.c
└── sketch.ino   (or src/main.cpp for PlatformIO)
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 Sim - Cycle 17",
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
        "magnet": "2",
        "rpm": "0"
      }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": -60,
      "left": 200,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": -60,
      "left": 230,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "v0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "v0" ] ],
    [ "esp:8",     "enc1:SDA", "green",  [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "blue",   [ "h0" ] ],
    [ "esp:GND.1", "enc1:DIR", "black",  [ "v0" ] ],
    [ "esp:4",     "enc1:OUT", "orange", [ "h0" ] ],

    [ "r1:1", "esp:3V3.1", "red",   [] ],
    [ "r1:2", "enc1:SDA",  "green", [] ],
    [ "r2:1", "esp:3V3.1", "red",   [] ],
    [ "r2:2", "enc1:SCL",  "blue",  [] ]
  ],
  "dependencies": {}
}
```

> The pull-up resistors are optional in Wokwi because the I2C bus is simulated, but including them mirrors real hardware. `DIR` tied to GND gives clockwise-increasing angles.

---

## 2. `wokwi.toml`

```toml
[wokwi]
version = 1
firmware = ".pio/build/esp32-s3-devkitc-1/firmware.bin"
elf = ".pio/build/esp32-s3-devkitc-1/firmware.elf"

[[chip]]
name = "as5600"
binary = "as5600.chip.wasm"
```

> On wokwi.com, skip this file. Instead, add the chip via **"+" → Custom Chip → C**, name it `as5600`, and paste the two files below.

---

## 3. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 17",
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
      "max": 359,
      "step": 1
    },
    {
      "id": "magnet",
      "label": "Field: 0=none 1=weak 2=ok 3=strong",
      "type": "range",
      "min": 0,
      "max": 3,
      "step": 1
    },
    {
      "id": "rpm",
      "label": "Spin speed (RPM, 0=manual)",
      "type": "range",
      "min": -120,
      "max": 120,
      "step": 1
    }
  ]
}
```

---

## 4. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder - Wokwi custom chip
// I2C address 0x36. Implements the main register map:
//   0x00 ZMCO, 0x01-02 ZPOS, 0x03-04 MPOS, 0x05-06 MANG, 0x07-08 CONF,
//   0x0B STATUS, 0x0C-0D RAW ANGLE, 0x0E-0F ANGLE, 0x1A AGC,
//   0x1B-1C MAGNITUDE, 0xFF BURN
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define AS5600_ADDR   0x36

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

#define TICK_US       1000       // internal update period (1 kHz)

typedef struct {
  pin_t    pin_out;
  pin_t    pin_dir;

  uint32_t attr_angle;
  uint32_t attr_magnet;
  uint32_t attr_rpm;

  uint8_t  reg_ptr;
  bool     first_write;          // first byte after START = register address

  // Programmable registers
  uint8_t  zmco;
  uint16_t zpos;
  uint16_t mpos;
  uint16_t mang;
  uint16_t conf;

  // Simulated state
  double   spin_offset_deg;      // accumulated rotation from rpm control
  uint16_t raw_angle;
  uint16_t angle;
  uint8_t  status;
  uint8_t  agc;
  uint16_t magnitude;

  // Latched 16-bit value for coherent multi-byte reads
  uint16_t latch_raw;
  uint16_t latch_angle;
  uint16_t latch_mag;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);
static void on_tick(void *user_data);

// ---------------------------------------------------------------------------
// Angle computation
// ---------------------------------------------------------------------------
static void update_sensor(chip_state_t *chip) {
  double deg = (double)attr_read(chip->attr_angle) + chip->spin_offset_deg;
  while (deg < 0)      deg += 360.0;
  while (deg >= 360.0) deg -= 360.0;

  uint16_t raw = (uint16_t)(deg * 4096.0 / 360.0) & 0x0FFF;

  // DIR pin: GND = clockwise increasing, VCC = counter-clockwise increasing
  if (pin_read(chip->pin_dir) == HIGH) {
    raw = (4096 - raw) & 0x0FFF;
  }

  // Magnet field model
  uint32_t mag_level = attr_read(chip->attr_magnet);
  chip->status = 0;
  switch (mag_level) {
    case 0:  // no magnet
      chip->agc = 255; chip->magnitude = 0;
      chip->status = STATUS_ML;
      raw = (uint16_t)