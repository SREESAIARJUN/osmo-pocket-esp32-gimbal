# DroneCAN Node Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

"Cycle 11" isn't a standard DroneCAN or ArduPilot term. I've treated it as your project's iteration label. If it refers to a specific requirement, such as a fixed scheduling slot or a frame budget, share it and I'll adapt the timing section.

---

## 1. Message Set Overview

| Role | DSDL Type | Data Type ID | Signature | Payload | Rate |
|---|---|---|---|---|---|
| Heartbeat | `uavcan.protocol.NodeStatus` | 341 | `0x0F0868D0C1A7C6F1` | 7 B (single frame) | 1 Hz (allowed range: 2 ms to 1000 ms period) |
| ESC telemetry | `uavcan.equipment.esc.Status` | 1034 | `0xA9AF28AEA2FBB254` | 14 B (multi-frame, 3 CAN 2.0 frames) | 10 to 50 Hz per ESC |
| ESC extended (optional) | `uavcan.equipment.esc.StatusExtended` | 1036 | see generated header | 8 B | 5 to 10 Hz |

These are standard DroneCAN DSDL types (`DroneCAN/DSDL`). Do not redefine them with new IDs. ArduPilot's `AP_DroneCAN` already decodes them on the flight controller side.

---

## 2. DSDL Definitions

### 2.1 Heartbeat: `uavcan/protocol/341.NodeStatus.uavcan`

```text
#
# Mandatory heartbeat. Every node must publish this at least once per second.
#
uint16 MAX_BROADCASTING_PERIOD_MS = 1000
uint16 MIN_BROADCASTING_PERIOD_MS = 2
uint16 OFFLINE_TIMEOUT_MS         = 3000

uint32 uptime_sec                  # Seconds since boot

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

uint3 sub_mode                     # Keep 0 unless defined by vendor

uint16 vendor_specific_status_code
```

Bit budget: 32 + 2 + 3 + 3 + 16 = 56 bits, which is 7 bytes and fits a single frame (7 data bytes plus the tail byte).

### 2.2 ESC Telemetry: `uavcan/equipment/esc/1034.Status.uavcan`

```text
#
# Generic ESC status. Published per ESC (esc_index distinguishes them).
#
uint32 error_count          # Cumulative, resets only on reboot

float16 voltage             # Volts
float16 current             # Amperes (negative = regen)
float16 temperature         # KELVIN (not °C)

int18 rpm                   # Mechanical RPM, signed for direction

uint7 power_rating_pct      # 0..127, % of max power, 127 = unknown/saturated

uint5 esc_index             # 0..31, matches RawCommand / ESC_INDEX mapping
```

Bit budget: 32 + 48 + 18 + 7 + 5 = 110 bits, which rounds up to 14 bytes. This needs a multi-frame transfer: 2 CRC bytes plus 14 data bytes across 3 classic CAN frames.

### 2.3 Optional: `uavcan/equipment/esc/1036.StatusExtended.uavcan`

```text
uint7  input_pct            # Input command, % of full scale
uint7  output_pct           # Actual output duty, %
int16  motor_temperature_degC
uint9  motor_angle          # Electrical/mechanical angle, if available
uint19 status_flags         # Vendor-defined fault/status bits
uint5  esc_index
```

Verify this layout against the `DSDL` submodule in your ArduPilot checkout (`modules/DroneCAN/DSDL`). Always use the generated header rather than hand-packing bits.

---

## 3. Generated C Bindings (dronecan_dsdlc)

ArduPilot generates these at build time via `modules/DroneCAN/dronecan_dsdlc`:

```c
#include <dronecan_msgs.h>
// uavcan_protocol_NodeStatus               -> UAVCAN_PROTOCOL_NODESTATUS_ID / _SIGNATURE / _MAX_SIZE
// uavcan_equipment_esc_Status              -> UAVCAN_EQUIPMENT_ESC_STATUS_ID / _SIGNATURE / _MAX_SIZE
// uavcan_equipment_esc_StatusExtended      -> UAVCAN_EQUIPMENT_ESC_STATUSEXTENDED_ID ...
```

---

## 4. Publishing Implementation (AP_Periph style, STM32H743)

### 4.1 Heartbeat

```cpp
// AP_Periph/can.cpp  (simplified)
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = periph_health();            // OK/WARNING/ERROR/CRITICAL
    msg.mode       = in_bootloader_update ? UAVCAN_PROTOCOL_NODESTATUS_MODE_SOFTWARE_UPDATE
                                          : UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = hal.util->available_memory() & 0xFFFF; // example

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// in the 1 Hz section of the main loop:
if (now - last_1Hz_ms >= 1000) {
    last_1Hz_ms = now;
    send_node_status();
}
```

### 4.2 ESC Telemetry

```cpp
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now = AP_HAL::millis();
    const uint32_t period_ms = 1000U / constrain_int16(g.esc_telem_rate, 1, 200);
    if (now - last_esc_telem_ms < period_ms) {
        return;
    }
    last_esc_telem_ms = now;

    for (uint8_t i = 0; i < ESC_NUMBERS; i++) {
        const uint8_t esc_index = i + g.esc_number[i];      // offset param
        if (!esc_telem.is_telemetry_active(1U << i)) {
            continue;                                       // don't publish stale data
        }

        uavcan_equipment_esc_Status pkt {};
        float f;
        int16_t t_cdeg;

        pkt.esc_index        = esc_index & 0x1F;
        pkt.voltage          = esc_telem.get_voltage(i, f) ? f : nanf("");
        pkt.current          = esc_telem.get_current(i, f) ? f : nanf("");
        pkt.temperature      = esc_telem.get_temperature(i, t_cdeg)
                               ? C_TO_KELVIN(t_cdeg * 0.01f) : nanf("");
        pkt.rpm              = esc_telem.get_raw_rpm(i, f)
                               ? constrain_int32(f, -131072, 131071) : 0;  // int18 range
        pkt.power_rating_pct = constrain_int16(esc_telem.get_power_pct(i), 0, 127);
        pkt.error_count      = esc_telem.get_error_count(i);

        uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
        const uint16_t len = uavcan_equipment_esc_Status_encode(&pk