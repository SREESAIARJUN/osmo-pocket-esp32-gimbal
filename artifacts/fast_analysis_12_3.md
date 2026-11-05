# DroneCAN Node Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

"Cycle 12" isn't a standard DroneCAN or ArduPilot term. This document assumes it is your internal iteration or revision tag. If it refers to a specific scheduler period or slot, see the timing table in §4 and adjust the rates accordingly.

This assumes the H743 runs **AP_Periph** and publishes the messages. If the H743 is the flight controller, the same definitions apply, but the FC consumes ESC status instead of publishing it.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

**Data type ID:** 341 (broadcast)
**Signature:** `0x0F0868D0C1A7C6F1`
**Max size:** 7 bytes, so it fits in a single classic CAN frame.

### DSDL (`341.NodeStatus.uavcan`)
```
uint16 MAX_BROADCASTING_PERIOD_MS = 1000
uint16 MIN_BROADCASTING_PERIOD_MS = 2
uint16 OFFLINE_TIMEOUT_MS         = 3000

uint32 uptime_sec

uint2 HEALTH_OK       = 0
uint2 HEALTH_WARNING  = 1
uint2 HEALTH_ERROR    = 2
uint2 HEALTH_CRITICAL = 3
uint2 health

uint3 MODE_OPERATIONAL     = 0
uint3 MODE_INITIALIZATION  = 1
uint3 MODE_MAINTENANCE     = 2
uint3 MODE_SOFTWARE_UPDATE = 3
uint3 MODE_OFFLINE         = 7
uint3 mode

uint3 sub_mode                      # reserved, set to 0
uint16 vendor_specific_status_code
```

### Bit layout (56 bits)
| Bits | Field |
|---|---|
| 0–31 | uptime_sec |
| 32–33 | health |
| 34–36 | mode |
| 37–39 | sub_mode |
| 40–55 | vendor_specific_status_code |

### Generated C struct (dronecan_dsdlc)
```c
struct uavcan_protocol_NodeStatus {
    uint32_t uptime_sec;
    uint8_t  health;     // 2 bits
    uint8_t  mode;       // 3 bits
    uint8_t  sub_mode;   // 3 bits
    uint16_t vendor_specific_status_code;
};
```

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

**Data type ID:** 1034 (broadcast)
**Signature:** `0xA9AF28AEA2FBB254`
**Size:** 110 bits, which pads to 14 bytes.

### DSDL (`1034.Status.uavcan`)
```
uint32  error_count        # resets with ESC
float16 voltage            # V
float16 current            # A, negative = regen
float16 temperature        # Kelvin
int18   rpm                # negative = reverse
uint7   power_rating_pct   # 0–127 %
uint5   esc_index          # 0–31
```

### Generated C struct
```c
struct uavcan_equipment_esc_Status {
    uint32_t error_count;
    float    voltage;           // encoded as float16
    float    current;
    float    temperature;       // K, i.e. degC + 273.15
    int32_t  rpm;               // 18-bit signed
    uint8_t  power_rating_pct;
    uint8_t  esc_index;
};
```

### Framing
| Bus | Frames per transfer |
|---|---|
| Classic CAN | 3 frames: 2 bytes of transfer CRC plus 14 bytes of payload, at 7 bytes per frame |
| CAN-FD (H743 FDCAN) | 1 frame. Use `!canfdout()` to select tail-array optimisation |

### Optional ArduPilot extension: `uavcan.equipment.esc.StatusExtended` (DTID 1036)
```
uint7  input_pct
uint7  output_pct
int9   motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```
Verify the DTID and signature against your `DroneCAN/DSDL` submodule revision before use.

---

## 3. Publishing Code (AP_Periph style)

```cpp
// ---------- Heartbeat: call at 1 Hz ----------
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = node_health;     // update from sensor and ESC faults
    msg.mode       = node_mode;       // INITIALIZATION -> OPERATIONAL
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = hal.util->available_memory() >> 4; // or custom status bits

    uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint32_t len = uavcan_protocol_NodeStatus_encode(&msg, buf, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buf, len);
}

// ---------- ESC telemetry: rate set by ESC_TELEM_RATE ----------
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (g.esc_telem_rate <= 0 ||
        now_ms - last_esc_telem_ms < 1000U / g.esc_telem_rate) {
        return;
    }
    last_esc_telem_ms = now_ms;

    for (uint8_t i = 0; i < ESC_NUMBERS; i++) {
        if (!esc_telem.is_telemetry_active(1U << i)) {
            continue;
        }
        uavcan_equipment_esc_Status pkt {};
        float f; int32_t rpm; int16_t temp_cdeg;

        pkt.esc_index   = g.esc_number[i] < 0 ? i : g.esc_number[i];
        pkt.voltage     = esc_telem.get_voltage(i, f) ? f : nanf("");
        pkt.current     = esc_telem.get_current(i, f) ? f : nanf("");
        pkt.temperature = esc_telem.get_temperature(i, temp_cdeg)
                          ? C_TO_KELVIN(temp_cdeg * 0.01f) : nanf("");
        pkt.rpm         = esc_telem.get_raw_rpm(i, f) ? constrain_int32(f, -131072, 131071) : 0;
        pkt.power_rating_pct = esc_telem.get_power_pct(i);   // 0..100
        pkt.error_count = esc_telem.get_error_count(i);

        uint8_t buf[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
        const uint32_t len = uavcan_equipment_esc_Status_encode(&pkt, buf, !canfdout());
        canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                         UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                         CANARD_TRANSFER_PRIORITY_LOW,
                         buf, len);
    }
}
```

Notes on the code:
- `r