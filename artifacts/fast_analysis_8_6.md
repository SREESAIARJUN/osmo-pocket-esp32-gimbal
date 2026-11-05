# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot on STM32H743

I don't know what "Cycle 8" refers to in your project, so this covers the standard DroneCAN v0 definitions ArduPilot uses, plus publishing code and STM32H743 integration. If Cycle 8 has its own constraints (custom IDs, rates or a vendor namespace), tell me and I'll adapt it.

---

## 1. Message Definitions (DSDL)

### 1.1 Heartbeat: `uavcan.protocol.NodeStatus`

- **Data Type ID:** 341 (broadcast)
- **Signature:** `0x0F0868D0C1A7C6F1`
- **Size:** 56 bits (7 bytes), which fits in a single classic CAN frame

```
# 341.NodeStatus.uavcan
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

### 1.2 ESC Telemetry: `uavcan.equipment.esc.Status`

- **Data Type ID:** 1034 (broadcast)
- **Signature:** `0xA9AF28AEA2FBB254`
- **Size:** 110 bits (14 bytes), sent as a multi-frame transfer. With the 2-byte transfer CRC that is 16 bytes, which takes 3 classic CAN frames.

```
# 1034.Status.uavcan
uint32  error_count          # cumulative, resets on power cycle
float16 voltage              # V
float16 current              # A (negative = regen)
float16 temperature          # Kelvin
int18   rpm                  # signed, negative = reverse
uint7   power_rating_pct     # 0..127 %, instantaneous demand
uint5   esc_index            # 0..31, matches RawCommand array index
```

### 1.3 Optional extended telemetry: `uavcan.equipment.esc.StatusExtended`

- **Data Type ID:** 1036
- Supported by recent ArduPilot (4.4 and later)

```
uint7  input_pct               # commanded input, %
uint7  output_pct              # actual output duty, %
int9   motor_temperature_degC
uint9  motor_angle             # deg, 0..360, if available
uint19 status_flags            # vendor-defined
uint5  esc_index
```

---

## 2. Publishing on AP_Periph (STM32H743)

### 2.1 hwdef configuration

```
# hwdef.dat (AP_Periph target, STM32H743)
MCU STM32H7xx STM32H743xx
FLASH_RESERVE_START_KB 128          # bootloader
define HAL_USE_FDCAN TRUE

PD0 CAN1_RX CAN1
PD1 CAN1_TX CAN1
# optional CAN2: PB5 CAN2_RX CAN2 / PB6 CAN2_TX CAN2

define HAL_PERIPH_ENABLE_RC_OUT              # receives esc.RawCommand
define HAL_WITH_ESC_TELEM 1
define HAL_PERIPH_ENABLE_ESC_TELEM_EXTENDED  # optional, if your branch supports it
define CAN_APP_NODE_NAME "org.example.esc_h743"
```

The H743 FDCAN peripheral is driven in classic CAN mode at 1 Mbit/s by default. DroneCAN v0 does not need CAN-FD.

### 2.2 Heartbeat publisher (1 Hz)

This mirrors the AP_Periph `can.cpp` pattern using the generated `dronecan_msgs.h` encoders.

```cpp
#include <dronecan_msgs.h>

void AP_Periph_FW::send_node_status(void)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = node_health;     // HEALTH_OK / WARNING / ERROR / CRITICAL
    msg.mode       = node_mode;       // OPERATIONAL after init completes
    msg.sub_mode   = 0;
    // Vendor status: e.g. bitmask of ESC faults
    msg.vendor_specific_status_code = esc_fault_bitmask();

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// in the main loop:
if (now - last_1Hz_ms >= 1000) {
    last_1Hz_ms = now;
    send_node_status();
}
```

### 2.3 ESC telemetry publisher (rate set by the `ESC_TELEM_RATE` parameter)

```cpp
void AP_Periph_FW::esc_telem_update(void)
{
    const uint32_t now_ms = AP_HAL::millis();
    const uint16_t rate_hz = g.esc_telem_rate;        // default 50
    if (rate_hz == 0 || now_ms - last_esc_telem_ms < 1000U / rate_hz) {
        return;
    }
    last_esc_telem_ms = now_ms;

    for (uint8_t i = 0; i < ESC_NUMBERS; i++) {
        if (!esc_telem.is_telemetry_active(1U << i)) {
            continue;
        }

        uavcan_equipment_esc_Status pkt {};
        const uint8_t idx = g.esc_number[i];          // remappable index
        float v, c, t_c, rpm;

        pkt.esc_index        = idx;
        pkt.voltage          = esc_telem.get_voltage(i, v) ? v : nanf("");
        pkt.current          = esc_telem.get_current(i, c) ? c : nanf("");
        pkt.temperature      = esc_telem.get_temperature(i, t_c)
                               ? C_TO_KELVIN(t_c) : nanf("");
        pkt.rpm              = esc_telem.get_raw_rpm(i, rpm)
                               ? constrain_int32(rpm, -131072, 131071) : 0;   // int18 range
        pkt.power_rating_pct = constrain_int16(output_pct(i), 0, 127);
        pkt.error_count      = esc_telem.get_error_count(i);

        uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
        const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer, !canfdout());

        canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                         UAVCAN_EQUIP