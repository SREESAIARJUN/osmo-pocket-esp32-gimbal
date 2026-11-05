# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3 (Cycle 30)

This project has five files:

```
project/
├── diagram.json
├── sketch.ino
├── as5600.chip.json
├── as5600.chip.c
└── wokwi.toml          (only needed for the VS Code / CLI flow)
```

---

## 1. `diagram.json`

The wiring uses the ESP32-S3 default I2C pins: SDA = GPIO8, SCL = GPIO9.

```json
{
  "version": 1,
  "author": "AS5600 Sim - Cycle 30",
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
      "top": 38.4,
      "left": 230.4,
      "attrs": { "angle": "0", "rpm": "0", "field": "60" }
    },
    {
      "type": "wokwi-slide-switch",
      "id": "swdir",
      "top": 172.8,
      "left": 249.6,
      "attrs": {}
    },
    {
      "type": "wokwi-led",
      "id": "ledmd",
      "top": -76.8,
      "left": 345.6,
      "attrs": { "color": "green", "label": "MD" }
    },
    {
      "type": "wokwi-resistor",
      "id": "r1",
      "top": -19.2,
      "left": 374.4,
      "attrs": { "value": "330" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "enc1:VDD", "esp:3V3.1", "red",    [ "h0" ] ],
    [ "enc1:GND", "esp:GND.1", "black",  [ "h0" ] ],
    [ "enc1:SDA", "esp:8",     "green",  [ "h0" ] ],
    [ "enc1:SCL", "esp:9",     "yellow", [ "h0" ] ],
    [ "enc1:OUT", "esp:4",     "orange", [ "h0" ] ],

    [ "swdir:2",  "enc1:DIR",  "blue",   [ "v0" ] ],
    [ "swdir:1",  "esp:GND.2", "black",  [ "v0" ] ],
    [ "swdir:3",  "esp:3V3.2", "red",    [ "v0" ] ],

    [ "esp:5",    "ledmd:A",   "green",  [ "h0" ] ],
    [ "ledmd:C",  "r1:1",      "black",  [ "v0" ] ],
    [ "r1:2",     "esp:GND.3", "black",  [ "v0" ] ]
  ],
  "dependencies": {}
}
```

---

## 2. `as5600.chip.json`

```json
{
  "name": "AS5600",
  "author": "Cycle 30",
  "pins": [
    "VDD",
    "OUT",
    "GND",
    "PGO",
    "SCL",
    "SDA",
    "DIR",
    ""
  ],
  "controls": [
    {
      "id": "angle",
      "label": "Magnet angle (°)",
      "type": "range",
      "min": 0,
      "max": 360,
      "step": 0.1
    },
    {
      "id": "rpm",
      "label": "Auto-rotate (RPM)",
      "type": "range",
      "min": -600,
      "max": 600,
      "step": 1
    },
    {
      "id": "field",
      "label": "Magnetic field (mT)",
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
// AS5600 12-bit magnetic rotary position sensor - Wokwi custom chip
// I2C address 0x36. Supports the ZMCO, ZPOS, MPOS, MANG, CONF, STATUS,
// RAW AN