# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3 (Cycle 19)

This project simulates an AS5600 magnetic angle encoder in Wokwi and connects it to an ESP32-S3 over I2C at address `0x36`. It includes five files:

- `diagram.json`: the wiring
- `as5600.chip.json`: the chip definition and interactive controls
- `as5600.chip.c`: the custom chip logic
- `sketch.ino`: an Arduino test sketch
- `wokwi.toml`: only needed for the VS Code / CLI workflow

## Project layout

```
as5600-esp32s3/
├── diagram.json
├── as5600.chip.json
├── as5600.chip.c
├── sketch.ino
└── wokwi.toml          (only needed for VS Code / CLI)
```

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "Cycle 19 - AS5600 on ESP32-S3",
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
      "left": 230,
      "attrs": { "angle": "0", "rpm": "0", "field": "60" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": -60,
      "left": 160,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r2",
      "top": -60,
      "left": 190,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VCC", "esp:3V3.1", "red",    [ "v0" ] ],
    [ "enc1:GND", "esp:GND.1", "black",  [ "v0" ] ],
    [ "enc1:SDA", "esp:8",     "green",  [ "h0" ] ],
    [ "enc1:SCL", "esp:9",     "yellow", [ "h0" ] ],
    [ "enc1:OUT", "esp:1",     "orange", [ "h0" ] ],
    [ "enc1:DIR", "esp:GND.1", "gray",   [ "v0" ] ],

    [ "r1:1", "esp:3V3.1", "red",    [] ],
    [ "r1:2", "enc1:SDA",  "green",  [] ],
    [ "r2:1", "esp:3V3.1", "red",    [] ],
    [ "r2:2", "enc1:SCL",  "yellow", [] ]
  ],
  "dependencies": {}
}
```

The wiring is:

| AS5600 | ESP32-S3 | Notes |
|---|---|---|
| VCC | 3V3 | 3.3 V supply |
| GND | GND | |
| SDA | GPIO 8 | Default S3 Wire SDA, 4.7 kΩ pull-up |
| SCL | GPIO 9 | Default S3 Wire SCL, 4.7 kΩ pull-up |
| OUT | GPIO 1 | ADC1_CH0, reads the analog output |
| DIR | GND | Clockwise = increasing angle |

To reverse the direction, tie DIR to 3V3 instead.

---

## 2. `as5600.chip.json`

```json
{
  