# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip Simulation on ESP32-S3

This setup includes four files:

- **`diagram.json`** wires the parts together.
- **`as5600.chip.json`** defines the chip's pins and live controls.
- **`as5600.chip.c`** implements the chip logic as an I2C slave at `0x36`, emulating the AS5600 register map.
- **`sketch.ino`** is an ESP32-S3 Arduino test program.

## Project Layout

```
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only for the VS Code / CLI workflow)
```

On wokwi.com, use the editor's "+" menu, choose "Custom chip (C)", and name it `as5600`. This creates `as5600.chip.json` and `as5600.chip.c`. Paste the contents below into those files.

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
      "attrs": {
        "angle": "0",
        "rpm": "0",
        "magnet": "50"
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

    [ "esp:3V3.1", "enc1:VCC", "red", [] ],
    [ "esp:GND.1", "enc1:GND", "black", [] ],
    [ "esp:8", "enc1:SDA", "green", [] ],
    [ "esp:9", "enc1:SCL", "blue", [] ],
    [ "esp:GND.1", "enc1:DIR", "gray", [] ],
    [ "esp:4", "enc1:OUT", "orange", [] ],

    [ "r1:1", "esp:3V3.1", "red", [] ],
    [ "r1:2", "esp:8", "green", [] ],
    [ "r2:1", "esp:3V3.1", "red", [] ],
    [ "r2:2", "esp:9", "blue", [] ]
  ],
  "dependencies": {}
}
```

Wiring notes:

- **I2C bus:** GPIO8 is SDA and GPIO9 is SCL, which are the ESP32-S3 Arduino defaults for `Wire`.
- **DIR pin:** tied to GND, so the angle increases clockwise. Tie it to 3V3 to reverse the direction.
- **OUT pin:** the analog output goes to GPIO4 (ADC1_CH3).
- **Pull-up resistors:** Wokwi's I2C simulation doesn't require them, but they are included for realism.

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
    "",
    ""
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
      "id": "rpm",
      "label": "Auto-spin (RPM, 0=off)",
      "type": "range",
      "min": -120,
      "max": 120,
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

## 3. `as5600.chip.c`

```c
// AS5600 12-bit magnetic rotary encoder – Wokwi custom chip
// I2C address 0x36. Emulates the register map, ZPOS/MPOS/MANG scaling,
// STATUS/AGC/MAGNITUDE, the DIR pin, and analog OUT.

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

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
#define REG_MAG_L     0x1C
#define REG_BURN      0xFF

// STATUS bits
#define STATUS_MH     (1 << 3)   // magnet too strong
#define STATUS_ML     (1 << 4)   // magnet too weak
#define STATUS_MD     (1 << 5)   // magnet detected

typedef struct {
  i2c_dev_t i2c;
  pin_t pin_dir;
  pin_t pin_out;

  uint32_t attr_angle;
  uint32_t attr_rpm;
  uint32_t attr_magnet;

  // writable configuration registers
  uint8_t  zmco;
  uint16_t zpos;   // 12-bit
  uint16_t mpos;   // 12-bit
  uint16_t mang;   // 12-bit
  uint16_t conf;   // 14-bit

  // I2C transaction state
  uint8_t  reg_ptr;
  bool     first_byte;   // next write byte is the register address

  // simulation state
  double   spin_offset_deg;
  uint16_t raw_angle;
  uint16_t angle;
  uint8_t  status;
  uint8_t  agc;
  uint16_t magnitude;
} chip_state_t;

static bool    on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool    on_i2c_write(void *user_data, uint8_t data);
static void    on_i2c_