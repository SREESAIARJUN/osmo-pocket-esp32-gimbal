# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

"Cycle 13" isn't a standard DroneCAN or ArduPilot term, so I've treated it as your project's iteration label. Everything below uses the standard DroneCAN v0 types that ArduPilot (AP_Periph and the flight controller side) already supports. You don't need custom DSDL unless you want vendor-specific fields (see section 6).

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus` (DTID 341)

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

uint3 sub_mode                     # reserved, set to 0
uint16 vendor_specific_status_code
```

**Size:** 56 bits (7 bytes), so it fits in a single classic CAN frame.

**Publish rate:** 1 Hz. A node is declared offline after 3 s without a heartbeat.

**Suggested `vendor_specific_status_code` bitmap** (this layout is my suggestion, not a standard):

| Bits | Meaning |
|------|---------|
| 0 | ESC telemetry link lost |
| 1 | Over-temperature (any ESC) |
| 2 | Under-voltage |
| 3 | Over-current |
| 4–7 | Number of active ESCs |
| 8–15 | Reserved / build cycle ID (e.g. 13) |

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status` (DTID 1034)

```
# uavcan/equipment/esc/1034.Status.uavcan
uint32  error_count          # resets on power cycle
float16 voltage              # Volts
float16 current              # Amps (negative = regen)
float16 temperature          # KELVIN
int18   rpm                  # signed, negative = reverse
uint7   power_rating_pct     # 0..127, % of max power
uint5   esc_index            # 0..31
```

**Size:** 110 bits (14 bytes). On classic CAN this becomes a multi-frame transfer: 14 bytes plus a 2-byte transfer CRC, sent as 3 frames. On CAN-FD it fits in a single frame.

---

## 3. Optional: `uavcan.equipment.esc.StatusExtended` (DTID 1036, DroneCAN)

```
uint7  input_pct            # throttle demand %
uint7  output_pct           # actual output %
int9   motor_temperature_degC
uint9  motor_angle          # 0..360 deg (if sensored)
uint19 status_flags         # vendor-defined
uint5  esc_index
```

Use this only if your FC firmware version consumes it. Check `AP_DroneCAN.cpp` in your ArduPilot branch before relying on it.

---

## 4. AP_Periph Publishing Code (STM32H743, libcanard + dronecan_dsdlc)

```cpp
// AP_Periph: heartbeat (called from loop at 1 Hz)
void AP_Periph_FW::send_node_status(void)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = node_health;        // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    msg.mode       = node_mode;          // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = build_vendor_status();

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

```cpp
// AP_Periph: ESC telemetry (rate-limited by ESC_TELEM_RATE)
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
        pkt.esc_index = i;

        float v, c, rpm; int16_t temp_cdeg;
        pkt.voltage     = esc_telem.get_voltage(i, v) ? v : nanf("");
        pkt.current     = esc_telem.get_current(i, c) ? c : nanf("");
        pkt.temperature = esc_telem.get_temperature(i, temp_cdeg)
                          ? C_TO_KELVIN(temp_cdeg * 0.01f) : nanf("");
        pkt.rpm         = esc_telem.get_raw_rpm(i, rpm)
                          ? constrain_int32(rpm, -131072, 131071) : 0;   // int18 range
        pkt.power_rating_pct = 0;                                         // fill if known
        pkt.error_count      = esc_telem.get_error_count(i);

        uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
        const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer, !canfdout());

        canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                         UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                         CANARD_TRANSFER_PRIORITY_LOW,
                         buffer, len);
    }
}
```

**Common pitfalls:**
- **Temperature must be in Kelvin.** ArduPilot converts it back to °C on the FC side, so sending °C produces readings around −250 °C.
- **Clamp RPM to the int18 range** (±131071). Higher values silently wrap.
- **Don't scale to float16 manually.** The encoder does the float16 conversion, so assign plain floats.
- **Match `esc_index` to the FC output.** It must match the FC's `CAN_Dx_UC_ESC_OF` offset plus the motor output index, or telemetry lands on the wrong motor.

---

## 5. Configuration and Bus Budget (H743 FDCAN @ 1 Mbit/s classic)

| Parameter | Typical value |
|-----------|---------------|
| `CAN_P1_DRIVER` / `CAN_D1_PROTOCOL` |