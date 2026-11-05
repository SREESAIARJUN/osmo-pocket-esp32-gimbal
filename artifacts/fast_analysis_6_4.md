# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

I don't know what "Cycle 6" refers to in your project, so this covers the full baseline specification. It includes:

- DSDL definitions
- Wire-level layout
- Publishing parameters
- ArduPilot integration code (AP_DroneCAN / AP_Periph style)
- STM32H743 FDCAN considerations

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

### DSDL (`341.NodeStatus.uavcan`)
```
# Abstract node status. Published by every node.
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

### Wire properties
| Property | Value |
|---|---|
| Data Type ID | 341 (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits = 7 bytes → single frame (8 bytes with tail byte) |
| Rate | 1 Hz (must be ≤ 1000 ms period) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` |
| Offline detection | Peers consider the node dead after 3000 ms of silence |

### Bit layout (little-endian, packed)
```
Byte 0-3 : uptime_sec (uint32)
Byte 4   : [1:0] health | [4:2] mode | [7:5] sub_mode
Byte 5-6 : vendor_specific_status_code
Byte 7   : tail byte (SOT=1, EOT=1, toggle, transfer_id[4:0])
```

### ArduPilot publisher
This follows the AP_DroneCAN pattern of ArduPilot 4.4+.

```cpp
// AP_DroneCAN.h
Canard::Publisher<uavcan_protocol_NodeStatus> node_status{canard_iface};
uavcan_protocol_NodeStatus node_status_msg;
uint32_t _node_status_last_send_ms;

// AP_DroneCAN.cpp
void AP_DroneCAN::send_node_status(void)
{
    const uint32_t now = AP_HAL::millis();
    if (now - _node_status_last_send_ms < 1000) {
        return;
    }
    _node_status_last_send_ms = now;

    node_status_msg.uptime_sec = now / 1000;
    node_status_msg.health     = hal.util->safety_switch_state() == AP_HAL::Util::SAFETY_DISARMED
                                   ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK
                                   : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK; // map real health here
    node_status_msg.mode       = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    node_status_msg.sub_mode   = 0;
    // Free-form: e.g. low byte = error flags, high byte = arming state
    node_status_msg.vendor_specific_status_code = 0;

    node_status.broadcast(node_status_msg);
}
```

### AP_Periph style (raw canard)
```cpp
static void send_node_status()
{
    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    node_status.uptime_sec = AP_HAL::millis() / 1000U;
    node_status.vendor_specific_status_code = hal.util->available_memory() & 0xFFFF;

    const uint16_t len = uavcan_protocol_NodeStatus_encode(&node_status, buffer, !periph.canfdout());
    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

### DSDL (`1034.Status.uavcan`)
```
# Generic ESC status. Published by each ESC, independently per esc_index.
uint32 error_count          # cumulative resets/faults/etc.

float16 voltage             # Volt
float16 current             # Ampere; negative = regenerative braking
float16 temperature         # Kelvin

int18 rpm                   # negative = reverse rotation

uint7 PERCENT_UNKNOWN = 127
uint7 power_rating_pct      # instantaneous demand vs. rated power, 0..100

uint5 esc_index             # 0..31, matches RawCommand array index
```

### Wire properties
| Property | Value |
|---|---|
| Data Type ID | 1034 (broadcast) |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 32 + 3×16 + 18 + 7 + 5 = 110 bits → 14 bytes |
| Classic CAN | Multi-frame: 2 frames (including CRC-16 and tail bytes) |
| CAN FD | Single frame (16-byte DLC) |
| Rate | 10–50 Hz per ESC (AP_Periph `ESC_TELEM_RATE`, default 50) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (keep commands at higher priority) |

### Bit layout (payload, before framing)
```
Bits   0-31  : error_count
Bits  32-47  : voltage     (IEEE754 half)
Bits  48-63  : current     (IEEE754 half)
Bits  64-79  : temperature (IEEE754 half, Kelvin)
Bits  80-97  : rpm         (int18, two's complement)
Bits  98-104 : power_rating_pct
Bits 105-109 : esc_index
```

### Optional companion: `uavcan.equipment.esc.StatusExtended` (ID 1036)
```
uint7  input_pct            # throttle demand 0..100
uint7  output_pct           # actual output duty 0..100
int9   motor_temperature_degC
uint9  motor_angle          # degrees, 0..360 (sensored motors)
uint19 status_flags         # vendor-defined fault bits
uint5  esc_index
```
Publish this at a lower rate (about 5–10 Hz) if you need motor temperature or fault flags. ArduPilot's AP_DroneCAN parses it into `AP_ESC_Telem`.

### ArduPilot publisher (AP_Periph ESC node)
```cpp
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (g.esc_telem_rate <= 0 ||
        now_ms - last_esc_telem_update_ms < 1000U / g.esc