# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot (STM32H743)

I couldn't determine what "Cycle 26" refers to. It may be an internal sprint, release train or test cycle. The definitions below follow the standard DroneCAN DSDL used by ArduPilot master / 4.5.x and later. If Cycle 26 implies a specific branch or custom vendor messages, let me know and I'll adjust.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits / 7 bytes (single CAN frame) |
| Rate | 1 Hz typical (allowed range: 2 ms to 1000 ms) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` |

```text
# uavcan/protocol/341.NodeStatus.uavcan
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
| Payload | 110 bits / 14 bytes (multi-frame on CAN 2.0, single frame on CAN-FD) |
| Rate | Set by `ESC_TELEM_RATE` (typically 10–50 Hz per ESC) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` |

```text
# uavcan/equipment/esc/1034.Status.uavcan
uint32  error_count         # cumulative, wraps
float16 voltage             # Volt
float16 current             # Ampere, negative = regen
float16 temperature         # KELVIN (not Celsius)
int18   rpm                 # negative = reverse
uint7   power_rating_pct    # 0..127, % of max power
uint5   esc_index           # 0..31
```

### Optional: `uavcan.equipment.esc.StatusExtended` (ID 1036)

ArduPilot also consumes this extended message for throttle in/out and motor temperature. Verify the field layout against the `dronecan/DSDL` revision pinned in your tree.

```text
uint7  input_pct            # demanded throttle %
uint7  output_pct           # actual output %
int9   motor_temperature_degC
uint9  motor_angle          # 0..360 deg, if sensored
uint19 status_flags         # manufacturer-specific
uint5  esc_index
```

---

## 3. AP_Periph Publishing Implementation (libcanard / dronecan_msgs)

```cpp
#include <dronecan_msgs.h>

// ---------- Heartbeat (called at 1 Hz from AP_Periph_FW::can_update) ----------
void AP_Periph_FW::node_status_send(void)
{
    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];

    node_status.uptime_sec = AP_HAL::millis() / 1000U;
    node_status.health     = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    node_status.mode       = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    node_status.sub_mode   = 0;
    node_status.vendor_specific_status_code = hal.util->available_memory() >> 4; // optional

    const uint16_t len = uavcan_protocol_NodeStatus_encode(&node_status, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                           , !canfdout()
#endif
                                                           );
    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// ---------- ESC telemetry (rate-limited by ESC_TELEM_RATE) ----------
#if AP_PERIPH_ESC_TELEM_ENABLED   // older trees: HAL_WITH_ESC_TELEM
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (g.esc_telem_rate <= 0 ||
        now_ms - last_esc_telem_update_ms < 1000U / g.esc_telem_rate) {
        return;
    }
    last_esc_telem_update_ms = now_ms;

    for (uint8_t i = 0; i < ESC_TELEM_MAX_ESCS; i++) {
        if (!esc_telem.is_telemetry_active(1U << i)) {
            continue;
        }
        uavcan_equipment_esc_Status pkt {};
        pkt.esc_index = g.esc_number[i];   // remap to vehicle motor index

        float v, c, rpm;
        int16_t temp_cdeg;
        pkt.voltage     = esc_telem.get_voltage(i, v)  ? v : nanf("");
        pkt.current     = esc_telem.get_current(i, c)  ? c : nanf("");
        pkt.rpm         = esc_telem.get_raw_rpm(i, rpm) ? int32_t(rpm) : 0;
        pkt.temperature = esc_telem.get_temperature(i, temp_cdeg)
                              ? C_TO_KELVIN(temp_cdeg * 0.01f) : nanf("");
        pkt.error_count      = 0;
        pkt.power_rating_pct = 0;

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
#endif
```

Newer trees replace raw `canard_broadcast` with `Canard::Publisher<uavcan_protocol_NodeStatus>` objects. The message structs and IDs are the same. Exact function and macro names drift between releases, so match them to your checkout.

---

## 4. STM32H743 hwdef Fragment (AP_Periph target)

```text
# hwdef.dat
MCU STM32H7xx STM32H743xx
FLASH_SIZE_KB 2048
APJ_BOARD_ID <your_board_id>
OSCILLATOR_HZ 8000000

# FDCAN1 (H7 uses FDCAN peripheral; CAN-FD capable)
PD0 CAN1_R