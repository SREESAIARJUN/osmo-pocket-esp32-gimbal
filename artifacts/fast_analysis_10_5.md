# DroneCAN Message Definitions: Node Heartbeat & ESC Telemetry

**Target:** ArduPilot (AP_Periph / main firmware) on STM32H743 (FDCAN1/FDCAN2)

> **Note:** I've interpreted "Cycle 10" as your project's iteration label. If it refers to a specific timing budget, scheduler slot, or spec revision, share it and I'll align the rates below.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits → **7 bytes** (single frame on classic CAN) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` |
| Rate | **1 Hz** (spec: 2 ms min, 1000 ms max, offline timeout 3000 ms) |

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

### Bit layout (little-endian, packed)
```
Byte: 0      1      2      3      4                5      6
     [ uptime_sec (32)          ][hh|mmm|sss]     [ vendor_status (16) ]
```

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 110 bits → **14 bytes** (multi-frame on classic CAN: 3 frames + transfer CRC; single frame on CAN FD) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (or MEDIUM for flight-critical feedback) |
| Rate | Typically **10–50 Hz per ESC**, round-robin across `esc_index` |

### DSDL (`1034.Status.uavcan`)
```
uint32  error_count         # Resets on reboot
float16 voltage             # Volt
float16 current             # Ampere; may be negative (regen braking)
float16 temperature         # Kelvin (!)
int18   rpm                 # Negative = reverse
uint7   power_rating_pct    # 0..127; 0 = idle, 100 = max power
uint5   esc_index           # 0..31
```

### Optional extension: `dronecan.equipment.esc.StatusExtended` (DTID 1036)
Used by ArduPilot for throttle in/out and motor temperature. Verify the ID against your `DroneCAN/DSDL` submodule revision:
```
uint7  input_pct            # Commanded throttle %
uint7  output_pct           # Actual output duty %
int9   motor_temperature_degC
uint9  motor_angle          # 0..360 deg, if available
uint19 status_flags         # Vendor-specific
uint5  esc_index
```

---

## 3. Publishing Implementation (AP_Periph style, dronecan_dsdlc-generated C)

```cpp
// ---- Heartbeat: call from the 1 Hz path of AP_Periph_FW::update() ----
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec                  = AP_HAL::millis() / 1000U;
    msg.health                      = node_health;   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    msg.mode                        = node_mode;     // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    msg.sub_mode                    = 0;
    msg.vendor_specific_status_code = vendor_status; // e.g. hal.util->available_memory() bucket

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                          , !canfdout()
#endif
                                                          );
    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// ---- ESC telemetry: rate-limited, one ESC per call (round-robin) ----
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (g.esc_telem_rate <= 0 ||
        now_ms - last_esc_telem_ms < 1000U / g.esc_telem_rate) {
        return;
    }
    last_esc_telem_ms = now_ms;

    static uint8_t idx;
    const uint8_t n_esc = MIN(g.esc_number, 32);   // uint5 limit
    if (n_esc == 0) return;
    idx = (idx + 1) % n_esc;

    const auto &t = esc_telem.get(idx);   // your telemetry source (BLHeli/DShot/etc.)
    if (!t.valid) return;                 // never publish stale data

    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = idx;
    pkt.error_count      = t.error_count;
    pkt.voltage          = t.voltage_v;
    pkt.current          = t.current_a;
    pkt.temperature      = C_TO_KELVIN(t.temperature_degC);  // spec requires Kelvin
    pkt.rpm              = constrain_int32(t.rpm, -131072, 131071); // int18 range
    pkt.power_rating_pct = constrain_int16(t.power_pct, 0, 127);    // uint7 range

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                            , !canfdout()
#endif
                                                            );
    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

> The float16 fields are converted by the generated encoder (`canardConvertNativeFloatToFloat16`), so assign native `float` values.

---

## 4. STM32H743-Specific Notes

| Topic | Guidance |
|---|---|
| **Peripheral** | H743 uses **FDCAN** (not bxCAN). ChibiOS `CANIface` for H7 handles this; set `CAN_D1_PROTOCOL = 1` (DroneCAN) on the flight controller. |
| **Message RAM** | FDCAN message RAM (10 KB shared) is split between instances; ArduPilot's H7 driver configures it, so don't overlap if adding custom filters. |
| **CAN FD** | If `CAN_D1_FDBITRATE` is set and `canfdout()` is true, ESC Status fits a single frame (TAO disabled). Mixed buses must stay classic CAN