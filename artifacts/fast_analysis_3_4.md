# DroneCAN Node Heartbeat and ESC Telemetry Message Definitions (ArduPilot, STM32H743)

This specification covers the two periodic broadcasts an ArduPilot DroneCAN node (typically AP_Periph on an H743) must publish:

1. **Heartbeat**: `uavcan.protocol.NodeStatus`
2. **ESC telemetry**: `uavcan.equipment.esc.Status`, plus the optional `StatusExtended`

> **Note on "Cycle 3":** I've treated this as the third design iteration. This revision fixes the DSDL layouts, the publish rates and priorities, and the encode/broadcast path. If Cycle 3 has a specific scope in your project (for example, CAN-FD only or a custom vendor message), tell me and I'll adjust.

---

## 1. DSDL Message Definitions

### 1.1 Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits = **7 bytes** (fits in a single classic CAN frame) |
| Rate | **1 Hz** (spec: period ≤ 1000 ms, ≥ 2 ms; node is considered offline after 3000 ms) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (24) |

```text
# 341.NodeStatus.uavcan
uint16 MAX_BROADCASTING_PERIOD_MS = 1000
uint16 MIN_BROADCASTING_PERIOD_MS = 2
uint16 OFFLINE_TIMEOUT_MS         = 3000

uint32 uptime_sec                     # seconds since boot, saturating

uint2 HEALTH_OK         = 0
uint2 HEALTH_WARNING    = 1
uint2 HEALTH_ERROR      = 2
uint2 HEALTH_CRITICAL   = 3
uint2 health

uint3 MODE_OPERATIONAL      = 0
uint3 MODE_INITIALIZATION   = 1
uint3 MODE_MAINTENANCE      = 2
uint3 MODE_SOFTWARE_UPDATE  = 3
uint3 MODE_OFFLINE          = 7
uint3 mode

uint3 sub_mode                        # reserved, set to 0

uint16 vendor_specific_status_code    # free for node-specific use
```

**Byte layout** (little-endian bit packing):

| Bits | Field |
|---|---|
| 0–31 | uptime_sec |
| 32–33 | health |
| 34–36 | mode |
| 37–39 | sub_mode |
| 40–55 | vendor_specific_status_code |

### 1.2 ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** (broadcast) |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 110 bits = **14 bytes**. Classic CAN needs 2 frames (multi-frame with CRC); CAN-FD needs 1 frame. |
| Rate | Per ESC, typically **10–50 Hz** (ArduPilot default is about 10 Hz per ESC, round-robin) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` |

```text
# 1034.Status.uavcan
# Generic ESC status. Unknown fields must be set to NaN (floats) or 0.

uint32 error_count          # cumulative, resets on power cycle

float16 voltage             # [Volt]
float16 current             # [Ampere]; negative = regen
float16 temperature         # [Kelvin]

int18 rpm                   # negative = reverse rotation

uint7 power_rating_pct      # 0..127 %, instantaneous demand
uint5 esc_index             # 0..31, matches RawCommand slot
```

### 1.3 Optional: `uavcan.equipment.esc.StatusExtended`

This message was added to the DroneCAN DSDL and is supported in recent ArduPilot releases.

| Property | Value |
|---|---|
| Data Type ID | **1036** |
| Payload | 7 bytes (single frame) |
| Rate | Same as, or lower than, `Status` |

```text
# 1036.StatusExtended.uavcan
uint7  input_pct            # throttle input demand, 0..100 %
uint7  output_pct           # actual output, 0..100 %
int9   motor_temperature_degC
uint9  motor_angle          # electrical angle, 0..360 (511 = unknown)
uint19 status_flags         # vendor/ESC-specific error flags
uint5  esc_index
```

---

## 2. Publishing Implementation (AP_Periph, STM32H743)

### 2.1 Hardware Definition (`hwdef.dat`)

```text
MCU STM32H7xx STM32H743xx
define HAL_PERIPH_ENABLE_RC_OUT      # ESC command consumer
define HAL_WITH_ESC_TELEM 1          # enables AP_ESC_Telem
define AP_PERIPH_ESC_TELEM_ENABLED 1 # hypothetical feature flag: verify against your branch's AP_Periph build
# FDCAN1 on PD0/PD1 (adjust to your board)
PD0 CAN1_RX CAN1
PD1 CAN1_TX CAN1
```

> **Caution:** The `AP_PERIPH_*` feature-flag names have changed across ArduPilot versions. Verify them against your branch before relying on them.

### 2.2 Heartbeat Publisher

Simplified from `Tools/AP_Periph/can.cpp`:

```cpp
#include <dronecan_msgs.h>

void AP_Periph_FW::node_status_send(void)
{
    uavcan_protocol_NodeStatus pkt {};

    pkt.uptime_sec = AP_HAL::millis() / 1000U;
    pkt.health     = node_status.health;   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    pkt.mode       = node_status.mode;     // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    pkt.sub_mode   = 0;
    pkt.vendor_specific_status_code = vendor_status_bits();  // e.g. hal.util->available_memory() / 4 in stock AP_Periph

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&pkt, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// In the main loop / process1HzTasks():
if (now_ms - last_1hz_ms >= 1000) {
    last_1hz_ms = now_ms;
    node_status_send();
}
```

**Health and mode policy:**

| Condition | health | mode |
|---|---|---|
| Boot, before params are loaded | OK | INITIALIZATION |
| Normal | OK | OPERATIONAL |
| ESC reports error_count increase or over-temperature | WARNING | OPERATIONAL |
| ESC telemetry timeout (> 500 ms) | ERROR | OPERATIONAL |
| Firmware update in progress | OK | SOFTW