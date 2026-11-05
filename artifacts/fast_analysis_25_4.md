# DroneCAN Heartbeat & ESC Telemetry Definitions for ArduPilot (STM32H743)

I don't recognize "Cycle 25" as a DroneCAN, ArduPilot or ST release designation. If it refers to an internal project iteration or a specific ArduPilot branch, tell me and I'll adjust. The definitions below match current DroneCAN DSDL and ArduPilot master (AP_DroneCAN / AP_Periph).

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Max payload | 7 bytes (single CAN frame) |
| Publish rate | 1 Hz nominal. Must not exceed 1000 ms. Peers time out a node after 3 s. |
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

uint3 sub_mode                      # reserved, set 0

uint16 vendor_specific_status_code
```

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 14 bytes (multi-frame on CAN 2.0, single frame on CAN-FD) |
| Publish rate | Per ESC, typically 10–50 Hz (`ESC_TELEM_RATE` in AP_Periph) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` or MEDIUM |

### DSDL (`1034.Status.uavcan`)
```
uint32 error_count          # cumulative resettable error counter
float16 voltage             # V
float16 current             # A (negative = regen)
float16 temperature         # Kelvin
int18 rpm                   # negative = reverse
uint7 power_rating_pct      # 0..127 %, instantaneous demand
uint5 esc_index             # 0..31, matches RawCommand index
```

### Optional: `uavcan.equipment.esc.StatusExtended` (DTID 1036)
This message was added for ArduPilot. Verify the DTID against your `DroneCAN/DSDL` checkout.
```
uint7  input_pct            # demanded throttle, %
uint7  output_pct           # actual output, %
int9   motor_temperature_degC
uint9  motor_angle          # electrical/mech angle, deg
uint19 status_flags         # vendor-defined
uint5  esc_index
```

---

## 3. ArduPilot Publishing Implementation (AP_Periph, libcanard)

### Heartbeat
```cpp
// libraries/AP_DroneCAN / Tools/AP_Periph/can.cpp style
#include <dronecan_msgs.h>

void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = periph_healthy() ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK
                                      : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING;
    msg.mode       = fw_update_active ? UAVCAN_PROTOCOL_NODESTATUS_MODE_SOFTWARE_UPDATE
                                      : UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = hal.util->available_memory() >> 4; // example

    uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buf, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buf, len);
}

// in the main CAN loop:
if (now_ms - last_node_status_ms >= 1000) {
    last_node_status_ms = now_ms;
    send_node_status();
}
```

### ESC Telemetry
```cpp
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
        float v, c, t; int16_t tdeg; float rpm;

        pkt.esc_index   = g.esc_number[i];          // ESC index on bus
        pkt.voltage     = esc_telem.get_voltage(i, v)     ? v : nanf("");
        pkt.current     = esc_telem.get_current(i, c)     ? c : nanf("");
        pkt.temperature = esc_telem.get_temperature(i, tdeg)
                              ? C_TO_KELVIN(tdeg * 0.01f) : nanf("");
        pkt.rpm         = esc_telem.get_raw_rpm(i, rpm)   ? int32_t(rpm) : 0;
        pkt.power_rating_pct = 0;                   // fill if available
        pkt.error_count = esc_telem.get_error_count(i);

        uint8_t buf[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
        const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buf, !canfdout());
        canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                         UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                         CANARD_TRANSFER_PRIORITY_LOW,
                         buf, len);
    }
}
```

---

## 4. STM32H743 / hwdef Notes

```
# hwdef.dat (AP_Periph target)
MCU STM32H7xx STM32H743xx
FLASH_SIZE_KB 2048
PD0 CAN1_RX CAN1
PD1 CAN1_TX CAN1
define HAL_PERIPH_ENABLE_RC_OUT
define HAL_WITH_ESC_TELEM 1
define HAL_CANFD_SUPPORTED 1          # H7 FDCAN supports CAN-FD
define CAN_APP_NODE_NAME "org.ardupilot.myesc"
```

- The H743 uses the **FDCAN** peripheral. If you enable CAN-FD (`CAN_Px_FDBITRATE`), ESC Status fits in a single frame, which reduces bus load.
- **Bus load at 1 Mbit/s classic CAN:**
  - Each ESC Status transfer takes about 3 frames, roughly 300 µs.
  - For 8 ESCs at 50 Hz, ESC telemetry uses about 12% of the bus.
  - NodeStatus adds a negligible amount.
- **Flight controller side:**
  - Set `CAN_D1_UC_ESC_BM` to map ESC indices.
  - Set `CAN_D1_UC_ESC_OF` to apply an index offset.
  - Received telemetry feeds `AP_ESC_Telem`, which drives the `ESC` log messages and the RPM-based harmonic notch.