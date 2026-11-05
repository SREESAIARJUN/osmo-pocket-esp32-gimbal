# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

I don't know what "Cycle 5" refers to in your project, such as a sprint, review gate or document revision. This answer covers the standard DroneCAN definitions and ArduPilot integration. Adjust rates and IDs to your cycle's requirements. Verify the signatures and ArduPilot API details below against your checked-out `DSDL/` and `libraries/AP_DroneCAN/` tree, because helper APIs have changed between releases.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

### DSDL (`uavcan/protocol/341.NodeStatus.uavcan`)
```
# Fixed data type ID: 341
# Data type signature: 0x0F0868D0C1A7C6F1

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

### Wire layout (56 bits, 7 bytes, single frame, no transfer CRC)
| Bits | Field | Notes |
|---|---|---|
| 0–31 | `uptime_sec` | Monotonic seconds since boot |
| 32–33 | `health` | Map from arming checks or internal faults |
| 34–36 | `mode` | `INITIALIZATION` until sensors are ready, then `OPERATIONAL` |
| 37–39 | `sub_mode` | 0 |
| 40–55 | `vendor_specific_status_code` | Project-defined bitfield, see below |

### Publishing requirements
- **Rate:** 1 Hz nominal. The period must not exceed 1000 ms.
- **Priority:** `CANARD_TRANSFER_PRIORITY_LOW`. Use a broadcast message transfer.
- **Liveness:** Peers declare the node offline after 3000 ms without a NodeStatus.
- Respond to `uavcan.protocol.GetNodeInfo` (ID 1). Its response embeds the same NodeStatus.

### Suggested `vendor_specific_status_code` map
| Bit | Meaning |
|---|---|
| 0 | Armed |
| 1 | Pre-arm checks failing |
| 2 | Bus-off or error-passive seen on any FDCAN |
| 3 | Scheduler overrun in the last period |
| 4–7 | Reserved |
| 8–15 | Firmware build sub-revision |

### ArduPilot flight-controller side (AP_DroneCAN)
```cpp
// AP_DroneCAN.h
Canard::Publisher<uavcan_protocol_NodeStatus> node_status{canard_iface};

// AP_DroneCAN.cpp  (called from the DroneCAN thread loop)
void AP_DroneCAN::send_node_status(void)
{
    const uint32_t now = AP_HAL::millis();
    if (now - _node_status_last_send_ms < 1000) {
        return;
    }
    _node_status_last_send_ms = now;

    node_status_msg.uptime_sec = now / 1000U;
    node_status_msg.health     = hal.util->get_soft_armed() ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK
                                                            : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    node_status_msg.mode       = _initialized ? UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL
                                              : UAVCAN_PROTOCOL_NODESTATUS_MODE_INITIALIZATION;
    node_status_msg.sub_mode   = 0;
    node_status_msg.vendor_specific_status_code = build_vssc();   // project bitfield

    node_status.broadcast(node_status_msg);
}
```
The `health` expression is a placeholder: both branches currently yield `OK`. Replace it with a real mapping from your fault and arming state.

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

### DSDL (`uavcan/equipment/esc/1034.Status.uavcan`)
```
# Fixed data type ID: 1034
# Data type signature: 0xA9AF28AEA2FBB254

uint32 error_count        # cumulative, wraps
float16 voltage           # Volt
float16 current           # Ampere (negative = regen)
float16 temperature       # Kelvin
int18 rpm                 # signed for reversible ESCs
uint7 power_rating_pct    # 0..127, % of max power
uint5 esc_index           # 0..31, matches RawCommand index
```

### Wire layout
- The payload is 110 bits, which rounds up to 14 bytes.
- On classic CAN 2.0 this is a multi-frame transfer of 3 frames. Each frame carries 7 payload bytes plus a tail byte.
- The transfer CRC is CRC-16-CCITT, seeded with the data type signature, and is prepended to the first frame.
- On FDCAN with CAN-FD enabled (H743 supports it, e.g. `CAN_D1_UC_OPTION` FD bit), it fits in a single frame.

### Optional companion: `uavcan.equipment.esc.StatusExtended` (ID 1036)
```
uint7  input_pct
uint7  output_pct
int9   motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```

### Publishing requirements
- **Rate:** 10–50 Hz per ESC. ArduPilot logs and uses RPM for harmonic notch; 50 Hz is a good target if bus load allows. Configure it with the `ESC_RATE`/telemetry-rate parameter on the periph.
- **Priority:** Default (`CANARD_TRANSFER_PRIORITY_MEDIUM`) is acceptable. Keep it below `esc.RawCommand`.
- **Index:** `esc_index` must equal the actuator index the FC uses in `esc.RawCommand`. This is set via `CAN_D1_UC_ESC_OF` / servo function mapping.
- **Bus load estimate:** 8 ESCs × 50 Hz × 3 frames = 1200 frames/s, about 15% of 1 Mbit/s classic CAN. Use CAN-FD or reduce the rate if you add other traffic.

### ESC-node side (AP_Periph on STM32H743)
```cpp
// AP_Periph.h
Canard::Publisher<uavcan_equipment_esc_Status> esc_status_pub{canard_iface};

// can.cpp / esc telemetry path
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now = AP_HAL::millis();
    if (now - last_esc_telem_ms < (1000U / g.esc_telem_rate)) {
        return;
    }
    last_esc_telem_ms = now;

    for (uint8_t i = 0; i < ESC_NUMBERS; i++) {
        if (!esc_telem.is_telemetry_active(1U << i)) {
            continue;
        }
        uavcan_equipment_esc_Status pkt {};
        float v, c, rpm; int16_t temp_cdeg;

        pkt.esc_index        = i + g.esc_number_offset;
        pkt.voltage          = esc_telem.get_voltage(i, v)          