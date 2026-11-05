# DroneCAN Node Heartbeat and ESC Telemetry Definitions for ArduPilot on STM32H743

This assumes an **AP_Periph** node (an ESC or ESC-telemetry bridge) on an STM32H743 using the FDCAN peripheral. Publishing uses `libcanard` with the generated `dronecan_msgs.h` headers.

I don't have context for what "Cycle 16" refers to in your project. Tell me the scope and I can adjust, for example a specific revision, rate budget or bus-load target.

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits = 7 bytes (single frame on CAN 2.0) |
| Rate | 1 Hz (spec: ≥ 2 ms period, ≤ 1000 ms) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (24) |

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

### Publisher (AP_Periph style)
```cpp
#include <dronecan_msgs.h>

void AP_Periph_FW::node_status_send(void)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = node_health();   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK, etc.
    msg.mode       = node_mode();     // UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL, etc.
    msg.sub_mode   = 0;

    // Example vendor code: bit0 = armed, bits1-7 = ESC fault mask
    msg.vendor_specific_status_code = vendor_status_bits();

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

// In the main loop
void AP_Periph_FW::process1HzTasks(uint64_t timestamp_usec)
{
    node_status_send();
}
```

**Operational rules:**
- Set `MODE_INITIALIZATION` until the ESC driver and sensors are up, then switch to `MODE_OPERATIONAL`.
- Set `MODE_SOFTWARE_UPDATE` while the bootloader or firmware update is active.
- The flight controller (`AP_DroneCAN`) marks the node offline after 3000 ms without a NodeStatus.

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** (broadcast) |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 110 bits → 14 bytes |
| Frames | 3 frames on CAN 2.0 (multi-frame with CRC); 1 frame on CAN-FD |
| Rate | Typically 10–50 Hz per ESC (set with `ESC_TELEM_RATE`) |
| Priority | `CANARD_TRANSFER_PRIORITY_MEDIUM` (16) |

### DSDL (`1034.Status.uavcan`)
```
uint32  error_count        # resets on power cycle only
float16 voltage            # Volt
float16 current            # Ampere, may be negative (regen)
float16 temperature        # Kelvin (ESC/FET temperature)
int18   rpm                # negative = reverse
uint7   power_rating_pct   # 0..127 %, % of max power
uint5   esc_index          # 0..31, matches index in esc.RawCommand
```

### Publisher
```cpp
void AP_Periph_FW::esc_telem_send(uint8_t esc_index,
                                  const AP_ESC_Telem_Backend::TelemetryData &td,
                                  float rpm)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = esc_index + g.esc_number;   // offset per node
    pkt.voltage          = td.voltage;
    pkt.current          = td.current;
    pkt.temperature      = C_TO_KELVIN(td.temperature_cdeg * 0.01f);
    pkt.rpm              = constrain_int32(lrintf(rpm), -131072, 131071); // int18 range
    pkt.power_rating_pct = 0;                          // fill if the ESC reports it
    pkt.error_count      = td.error_count;             // or local CRC/timeout counter

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                            , !canfdout()
#endif
                                                            );

    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_MEDIUM,
                     buffer, len);
}

// Rate-limited round-robin over ESCs to spread bus load
void AP_Periph_FW::esc_telem_update(void)
{
    const uint32_t now_ms = AP_HAL::millis();
    const uint32_t period_ms = 1000U / MAX(1U, (uint32_t)g.esc_telem_rate.get());
    if (now_ms - last_esc_telem_ms < period_ms / num_escs) {
        return;
    }
    last_esc_telem_ms = now_ms;
    // send one ESC per slot
    esc_telem_send(next_esc, telem[next_esc], rpm[next_esc]);
    next_esc = (next_esc + 1) % num_escs;
}
```

### Optional extension: `uavcan.equipment.esc.StatusExtended` (ID 1036)
DroneCAN adds this message for input and output duty, motor temperature and status flags:
```
uint7  input_pct
uint7  output_pct
int9   motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```
- Publish it at a lower rate than `esc.Status`, for example 5–10 Hz.
- Check that your ArduPilot flight-controller version consumes it before relying on it.

---

## 3. STM32H743 / FDCAN Considerations

- **Transfer IDs:** `canard_broadc