# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot (STM32H743)

I don't know what "Cycle 22" refers to in your project, so I haven't tied anything to it. The definitions below match the upstream DroneCAN DSDL and ArduPilot's `AP_Periph` / `AP_DroneCAN` implementation. Check them against the `modules/DroneCAN/DSDL` revision pinned in your tree.

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Max payload | 7 bytes (single frame) |
| Publish rate | 1 Hz nominal. Must be ≤ 1000 ms and ≥ 2 ms. |
| Offline timeout | 3000 ms |

### DSDL (`uavcan/protocol/341.NodeStatus.uavcan`)
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

**Bit layout:** 32 + 2 + 3 + 3 + 16 = 56 bits, which is 7 bytes.

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** (broadcast) |
| Signature | `0xA9AF28AEA2FBB254` |
| Max payload | 14 bytes (multi-frame, 2 CAN 2.0 frames) |
| Publish rate | Configurable. AP_Periph uses `ESC_TELEM_RATE`, typically 10–50 Hz per ESC. |

### DSDL (`uavcan/equipment/esc/1034.Status.uavcan`)
```
uint32  error_count        # cumulative error counter
float16 voltage            # Volt
float16 current            # Ampere, negative = regenerative braking
float16 temperature        # Kelvin
int18   rpm                # negative = reverse rotation
uint7   power_rating_pct   # 0..127, instantaneous demand %
uint5   esc_index          # 0..31, matches RawCommand index
```

**Bit layout:** 32 + 16 + 16 + 16 + 18 + 7 + 5 = 110 bits, which rounds up to 14 bytes.

### Optional: `uavcan.equipment.esc.StatusExtended` (DroneCAN extension)
| Data Type ID | **1036** |
|---|---|
```
uint7  input_pct              # commanded throttle %
uint7  output_pct             # actual output duty %
int9   motor_temperature_degC
uint9  motor_angle            # electrical/mechanical angle, 0..360
uint19 status_flags           # vendor-defined
uint5  esc_index
```
This is 56 bits, so 7 bytes.

---

## 3. Publishing Implementation (AP_Periph style, libcanard + `dronecan_msgs.h`)

```cpp
#include <dronecan_msgs.h>

// ---------- NodeStatus (1 Hz) ----------
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec  = AP_HAL::millis() / 1000U;
    msg.health      = node_health;   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    msg.mode        = node_mode;     // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    msg.sub_mode    = 0;
    msg.vendor_specific_status_code = vendor_status; // e.g. CPU load / fault bits

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

// ---------- ESC Status (rate-limited per ESC) ----------
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (now_ms - last_esc_telem_ms < 1000U / MAX(g.esc_telem_rate.get(), 1)) {
        return;
    }
    last_esc_telem_ms = now_ms;

    for (uint8_t i = 0; i < ESC_NUMBERS; i++) {
        const int8_t motor = g.esc_number[i];   // maps local ESC -> esc_index
        if (motor < 0) continue;

        uavcan_equipment_esc_Status pkt {};
        float v, c, t; int32_t rpm;

        pkt.esc_index        = motor;                      // 0..31
        pkt.voltage          = esc_telem.get_voltage(i, v) ? v : nanf("");
        pkt.current          = esc_telem.get_current(i, c) ? c : nanf("");
        pkt.temperature      = esc_telem.get_temperature(i, t)
                               ? C_TO_KELVIN(t) : nanf("");   // Kelvin!
        pkt.rpm              = esc_telem.get_rpm(i, rpm)
                               ? constrain_int32(rpm, -131072, 131071) : 0; // int18
        pkt.power_rating_pct = constrain_int16(esc_telem.get_power_pct(i), 0, 127);
        pkt.error_count      = esc_telem.get_error_count(i);

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
}
```

---

## 4. STM32H743-Specific Notes

- **FDCAN peripheral:** The H743 uses FDCAN1/2, not bxCAN. ChibiOS `CANFDIface` handles this. Classic CAN 2.0 at 1 Mbit/s is the default, and CAN-FD is optional (`CAN_Dx_FDBITRATE`).
- **Tail Array Optimization (TAO):** With CAN-FD enabled, encoding differs, so keep the `!canfdout()` argument consistent.
- **Bus load:** One `esc.Status` takes 2 frames. Eight ESCs at 50 Hz is 800 frames/s, roughly 10% of a 1 Mbit/s bus. Size `ESC_TELEM_RATE` accordingly.
- **Priority:** NodeStatus and ESC Status use LOW priority. ESC `RawCommand` (ID 1030) from the autopilot should stay higher, so telemetry never delays commands.
- **Autopilot side:** `AP_DroneCAN::handle_ESC_status()` maps `esc_index` to the motor number