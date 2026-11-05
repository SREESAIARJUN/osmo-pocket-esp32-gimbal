# DroneCAN Heartbeat and ESC Telemetry for ArduPilot on STM32H743

I don't know what "Cycle 31" refers to (a sprint, a hardware revision, or an internal spec number). The definitions below follow the **standard DroneCAN DSDL** that ArduPilot ships in `modules/DroneCAN/DSDL`. If Cycle 31 requires custom fields, they belong in a vendor-specific message (see section 5) rather than in changes to these standard types.

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus` (Data Type ID 341)

**DSDL** (`uavcan/protocol/341.NodeStatus.uavcan`):

```
# Mandatory node heartbeat. Must be published at 1 Hz minimum.
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

- **Payload:** 56 bits (7 bytes). This fits in a single classic CAN frame.
- **Priority:** `CANARD_TRANSFER_PRIORITY_LOW`.
- **Rate:** 1 Hz.

## 2. ESC Telemetry: `uavcan.equipment.esc.Status` (Data Type ID 1034)

**DSDL** (`uavcan/equipment/esc/1034.Status.uavcan`):

```
# Generic ESC status. Unknown fields should be set to NaN (floats) or 0.
uint32  error_count        # resets on power cycle
float16 voltage            # Volts
float16 current            # Amps; negative = regenerative braking
float16 temperature        # KELVIN (not Celsius)
int18   rpm                # negative = reverse
uint7   power_rating_pct   # instant demand / max, 0..127 %
uint5   esc_index          # 0..31, matches RawCommand array index
```

- **Payload:** 110 bits (14 bytes). This needs a multi-frame transfer on classic CAN, or a single frame on CAN-FD.
- **Priority:** `CANARD_TRANSFER_PRIORITY_LOW`. Use MEDIUM only if the bus is lightly loaded.
- **Rate:** typically 10–50 Hz per ESC. In AP_Periph this is set by `ESC_TELEM_RATE`.

**Optional extension:** newer DSDL also defines `uavcan.equipment.esc.StatusExtended` (ID 1036), with these fields:
- `input_pct`
- `output_pct`
- `motor_temperature_degC`
- `motor_angle`
- `status_flags`
- `esc_index`

Check that your DSDL submodule revision includes it before relying on it.

---

## 3. Publishing Code (AP_Periph style, libcanard + generated `dronecan_msgs.h`)

```cpp
#include <dronecan_msgs.h>

// ---------- Heartbeat (call from 1 Hz loop) ----------
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus ns {};
    ns.uptime_sec  = AP_HAL::millis() / 1000U;
    ns.health      = node_health;   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    ns.mode        = node_mode;     // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    ns.sub_mode    = 0;
    ns.vendor_specific_status_code = vendor_status; // e.g. bitmask of faults

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&ns, buffer
#if CANARD_ENABLE_TAO_OPTION
        , !canfdout()
#endif
    );

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// ---------- ESC telemetry (call at ESC_TELEM_RATE) ----------
void AP_Periph_FW::send_esc_status(uint8_t esc_idx)
{
    const auto &esc = AP::esc_telem();
    uavcan_equipment_esc_Status pkt {};

    float v, i;
    int16_t temp_cdeg;
    float rpm;

    pkt.esc_index   = esc_idx;
    pkt.voltage     = esc.get_voltage(esc_idx, v)   ? v : nanf("");
    pkt.current     = esc.get_current(esc_idx, i)   ? i : nanf("");
    pkt.temperature = esc.get_temperature(esc_idx, temp_cdeg)
                          ? C_TO_KELVIN(temp_cdeg * 0.01f) : nanf("");
    pkt.rpm         = esc.get_raw_rpm(esc_idx, rpm)
                          ? constrain_int32(rpm, -131072, 131071) : 0; // int18 range
    pkt.power_rating_pct = constrain_int16(demand_pct[esc_idx], 0, 127);
    pkt.error_count = esc_error_count[esc_idx];

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

**Common pitfalls:**
- **Temperature units:** temperature must be in **Kelvin**. Sending Celsius is the most common bug, and the autopilot will log ~-250 °C.
- **RPM range:** `rpm` is int18, so clamp it to ±131071.
- **`esc_index` mapping:** `esc_index` must match the index the flight controller uses in `uavcan.equipment.esc.RawCommand`. On the autopilot side this is affected by `CAN_D1_UC_ESC_OF` (ESC offset) and `CAN_D1_UC_ESC_BM`.

---

## 4. STM32H743 hwdef Fragment

```
# FDCAN1 on PD0/PD1 (adjust to your board)
PD0 CAN1_RX CAN1
PD1 CAN1_TX CAN1
# optional transceiver silent/standby pin
PD3 GPIO_CAN1_SILENT OUTPUT PUSHPULL SPEED_LOW LOW

define HAL_PERIPH_ENABLE_ESC_TELEM      # AP_Periph only
define AP_PERIPH_ESC_TELEM_ENABLED 1
define HAL_CAN_DEFAULT_NODE_ID 0