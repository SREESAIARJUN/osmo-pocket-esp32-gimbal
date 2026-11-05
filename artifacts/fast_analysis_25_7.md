# AS5600 Magnetic Angle Encoder: Wokwi Custom Chip for ESP32-S3

This is a complete Wokwi project. A custom chip emulates the AS5600 over I²C at address `0x36`. The ESP32-S3 sketch reads the emulated encoder over I²C. The model covers:

- **Registers:** the full register map, including ZPOS/MPOS/MANG scaling, CONF, STATUS, AGC and MAGNITUDE.
- **Pointer behavior:** auto-increment, plus the AS5600 wrap-back on the angle and magnitude register pairs. This lets you poll an angle continuously without re-addressing.
- **DIR pin:** sets the counting direction.
- **OUT pin:** analog output, either full range or the reduced 10–90% range.
- **Live controls:** an angle slider, an RPM auto-spin and a magnet-strength slider. Magnet strength drives the MD/ML/MH status bits and AGC.

> I wasn't sure what "Cycle 25" refers to, so I used it only as a revision tag in the files. If it means a specific requirement, such as a 25 ms polling cycle, the sketch's `POLL_MS` constant is set to 25 for that.

---

## Project layout

```
as5600-esp32s3/
├── diagram.json
├── wokwi.toml
├── sketch.ino
└── chips/
    ├── as5600.chip.json
    └── as5600.chip.c
```

On wokwi.com you can instead add the chip through **"+" → Custom Chip → C**. Name it `as5600` and paste in the two chip files below.

---

## 1. `diagram.json`

```json
{
  "version": 1,
  "author": "AS5600 sim - Cycle 25",
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
      "id": "rsda",
      "top": -60,
      "left": 200,
      "rotate": 90,
      "attrs": { "value": "4700" }
    },
    {
      "type": "wokwi-resistor",
      "id": "rscl",
      "top": -60,
      "left": 230,
      "rotate": 90,
      "attrs": { "value": "4700" }
    }
  ],
  "connections": [
    [ "esp:TX", "$serialMonitor:RX", "", [] ],
    [ "esp:RX", "$serialMonitor:TX", "", [] ],

    [ "esp:3V3.1", "enc1:VCC", "red",    [ "h0" ] ],
    [ "esp:GND.1", "enc1:GND", "black",  [ "h0" ] ],
    [ "esp:8",     "enc1:SDA", "green",  [ "h0" ] ],
    [ "esp:9",     "enc1:SCL", "blue",   [ "h0" ] ],
    [ "esp:GND.1", "enc1:DIR", "black",  [ "h0" ] ],
    [ "esp:4",     "enc1:OUT", "orange", [ "h0" ] ],

    [ "rsda:1", "esp:3V3.1", "red",   [] ],
    [ "rsda:2", "enc1:SDA",  "green", [] ],
    [ "rscl:1", "esp:3V3.1", "red",   [] ],
    [ "rscl:2", "enc1:SCL",  "blue",  [] ]
  ],
  "dependencies": {}
}
```

**DIR wiring.** DIR is tied to GND, so the angle increases clockwise. Wire