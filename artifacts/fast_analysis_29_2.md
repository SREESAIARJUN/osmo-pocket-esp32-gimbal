# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot on STM32H743

"Cycle 29" isn't a standard DroneCAN or ArduPilot term, so I've treated it as your own iteration label. The definitions below follow the upstream DroneCAN DSDL (`dronecan/DSDL`) and the AP_Periph conventions used on H7 targets.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

**Data Type ID:** 341 (broadcast)
**Payload:** 56 bits, which is 7 bytes and fits in a single classic CAN frame.

```
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

uint3 sub_mode                        # reserved, set to 0
uint16 vendor_specific_status_code
```

**Publishing rules:**
- Publish at 1 Hz, which is the ArduPilot standard.
- Peers declare the node offline after 3 s without a heartbeat.
- Use `CANARD_TRANSFER_PRIORITY_LOW`.

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

**Data Type ID:** 1034 (broadcast)
**Payload:** 110 bits, which is 14 bytes. On classic CAN this becomes a multi-frame transfer of 3 frames including the transfer CRC. On FD-CAN it fits in a single frame.

```
# uavcan/equipment/esc/1034.Status.uavcan
uint32 error_count          # resets on ESC reboot
float16 voltage             # Volts
float16 current             # Amperes, negative = regen
float16 temperature         # KELVIN (not °C)
int18 rpm                   # negative = reverse
uint7 power_rating_pct      # 0..127, % of max
uint5 esc_index             # 0..31
```

### Optional: `uavcan.equipment.esc.StatusExtended` (ID 1036, DroneCAN extension)

This message is 56 bits (7 bytes) and fits in a single frame.

```
uint7  input_pct              # demanded throttle 0..100
uint7  output_pct             # actual output 0..100
int9   motor_temperature_degC # -256..255 °C
uint9  motor_angle            # electrical angle 0..360
uint19 status_flags           # vendor-defined
uint5  esc_index
```

**Publishing rules:**
- Publish ESC telemetry at 10–50 Hz per ESC. In AP_Periph this is set by the `ESC_TELEM_RATE` parameter.
- Use `CANARD_TRANSFER_PRIORITY_LOW` so telemetry does not starve `esc.RawCommand` (ID 1030) inbound traffic.

---

## 3. AP_Periph Publishing Implementation

The code uses the dronecan_dsdlc-generated headers.

```cpp
#include <dronecan_msgs.h>

// ---------- Heartbeat ----------
void AP_Periph_FW::node_status_send(void)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec  = AP_HAL::millis() / 1000U;
    msg.health      = node_health;   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    msg.mode        = node_mode;     // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    msg.sub_mode    = 0;
    msg.vendor_specific_status_code = vendor_status; // e.g. hal.util->available_memory()

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// Called from the main loop
void AP_Periph_FW::process1HzTasks(uint64_t now_us)
{
    node_status_send();
}

// ---------- ESC telemetry ----------
void AP_Periph_FW::esc_telem_send(uint8_t idx, const AP_ESC_Telem_Backend::TelemetryData &t)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = idx;
    pkt.error_count      = t.error_count;
    pkt.voltage          = t.voltage;
    pkt.current          = t.current;
    pkt.temperature      = C_TO_KELVIN(t.temperature_cdeg * 0.01f);
    pkt.rpm              = constrain_int32(rpm_for(idx), -131072, 131071); // int18 range
    pkt.power_rating_pct = constrain_int16(t.power_pct, 0, 127);

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer, !canfdout());

    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (g.esc_telem_rate <= 0 ||
        now_ms - last_esc_telem_ms < 1000U / g.esc_telem_rate) {
        return;
    }
    last_esc_telem_ms = now_ms;

    for (uint8_t i = 0; i < ESC_TELEM_MAX_ESCS; i++) {
        AP_ESC_Telem_Backend::TelemetryData t;
        if (esc_telem.get_telem_data(i, t)) {     // skip stale ESCs
            esc_telem_send(i + g.esc_number_offset, t);
        }
    }
}
```

---

## 4. STM32H743 hwdef Notes

```
# hwdef.dat
define HAL_PERIPH_ENABLE_ESC_TELEM   # or HAL_PERIPH_ENABLE_RC_OUT + HAL_WITH_ESC_TELEM
CAN_ORDER 1
PD0 CAN1_RX CAN1
PD1 CAN1_TX CAN1
define HAL_CAN_POOL_SIZE 6000
define CANARD_ENABLE_CANFD 1         # H7 FDCAN supports it; esc.Status becomes single-frame
```

**Bus load estimate (classic CAN, 1 Mbit/s):**
- Each `esc.Status` transfer is about 3 frames × ~130 bits, which is roughly 390 bits.
- 8 ESCs at 50 Hz therefore use about 156 kbit/s, or roughly 16% of the bus.
- NodeStatus traffic is negligible.

---

## 5. Validation Checklist

- [ ] Run `dronecan_gui_tool` and confirm NodeStatus appears at 1 Hz with `MODE_OPERATIONAL` and `HEALTH_OK`.
- [ ] Confir