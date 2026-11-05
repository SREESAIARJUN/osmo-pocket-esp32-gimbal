# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

I don't know what "Cycle 9" refers to in your project, so I haven't tied anything to it. Tell me what it covers, such as a requirement baseline or rate budget, and I can adapt this. The definitions below follow the standard DroneCAN DSDL used by ArduPilot (`modules/DroneCAN/DSDL`) and the AP_Periph and AP_DroneCAN publishing patterns.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

**Data Type ID:** 341 (broadcast)

### DSDL (`341.NodeStatus.uavcan`)
```
# Abstract node status information. Every node must publish this.

uint16 MAX_BROADCASTING_PERIOD_MS = 1000
uint16 MIN_BROADCASTING_PERIOD_MS = 2
uint16 OFFLINE_TIMEOUT_MS         = 3000

uint32 uptime_sec                 # Seconds since node start

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

uint3 sub_mode                    # Reserved, set to 0
uint16 vendor_specific_status_code
```

### Encoding Summary
| Field | Bits | Notes |
|---|---|---|
| uptime_sec | 32 | Monotonic, from `AP_HAL::millis()/1000` |
| health | 2 | Derived from arming/prearm/sensor faults |
| mode | 3 | OPERATIONAL after init completes |
| sub_mode | 3 | 0 |
| vendor_specific_status_code | 16 | Free use (e.g., error bitmask) |
| **Total** | **56 bits = 7 bytes** | Fits in a single classic CAN frame (7 data bytes + tail byte) |

### Publishing Requirements
- **Rate:** 1 Hz nominal. The DSDL permits a period between 2 ms and 1000 ms.
- **Liveness:** Peers declare the node offline after 3000 ms without a NodeStatus.
- **Priority:** Low (`CANARD_TRANSFER_PRIORITY_LOW`, about 24).
- **Prerequisite:** A valid node ID, set statically or obtained via DNA (`uavcan.protocol.dynamic_node_id.Allocation`, ID 1).

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

**Data Type ID:** 1034 (broadcast)

### DSDL (`1034.Status.uavcan`)
```
# Generic ESC status. Unknown fields should be set to NAN / 0.

uint32 error_count        # Resets on ESC power-up

float16 voltage           # Volt
float16 current           # Ampere. Can be negative (regen)
float16 temperature       # Kelvin

int18 rpm                 # Negative = reverse rotation

uint7 power_rating_pct    # Instant demand/max, 0..127 %

uint5 esc_index           # 0..31
```

### Encoding Summary
| Field | Bits | ArduPilot source |
|---|---|---|
| error_count | 32 | ESC-reported or CRC error counter |
| voltage | 16 (fp16) | `AP_ESC_Telem_Backend::TelemetryData::voltage` |
| current | 16 (fp16) | `.current` |
| temperature | 16 (fp16) | `.temperature_cdeg` converted to K (`C + 273.15`) |
| rpm | 18 (signed) | `AP_ESC_Telem::get_rpm()` |
| power_rating_pct | 7 | Throttle output %, clamped to 0–127 |
| esc_index | 5 | Motor/ESC index |
| **Total** | **110 bits → 14 bytes** | Multi-frame transfer: 14 + 2 CRC bytes = 16 bytes → **3 frames** on classic CAN, or 1 frame on CAN-FD |

### Optional Companion: `uavcan.equipment.esc.StatusExtended` (ID 1036)
This message exists in recent DroneCAN DSDL. Verify the exact layout against your submodule revision.
```
uint7  input_pct               # Commanded input %
uint7  output_pct              # Actual output %
int16  motor_temperature_degC
uint9  motor_angle             # Electrical angle, degrees
uint19 status_flags            # Vendor/ESC-specific flags
uint5  esc_index
```

### Publishing Requirements
- **Rate:** Per-ESC, typically 10–50 Hz. In AP_Periph this is set by the `ESC_TELEM_RATE` parameter.
- **Scheduling:** Round-robin over ESC indices per cycle to avoid bursts. With N ESCs at R Hz, the bus load is about N × R × 3 frames.
- **Priority:** Medium or low. Telemetry must never preempt `esc.RawCommand` (ID 1030), which should run at high priority.
- **Unknown values:** Set to `NAN` for fp16 fields and 0 for integers.

---

## 3. Bus Load Estimate (1 Mbit/s classic CAN)

The figures below assume 8 ESCs at 50 Hz for Status, 1 Hz heartbeat, and roughly 130 bits per extended frame including stuffing.

| Message | Frames/s | Bits/s | Load |
|---|---|---|---|
| NodeStatus | 1 | ~130 | <0.1 % |
| esc.Status ×8 @ 50 Hz | 1200 | ~156 k | ~15.6 % |
| esc.RawCommand @ 400 Hz (2 frames, 8 ch) | 800 | ~104 k | ~10.4 % |

Keep total load under about 40–50 % for deterministic latency. If load is a concern, enable CAN-FD on the H743, which supports it natively, so each Status transfer fits in a single frame.

---

## 4. ArduPilot Implementation Sketch (AP_Periph style)

```cpp
#include <dronecan_msgs.h>

// --- Heartbeat --------------------------------------------------------
static uavcan_protocol_NodeStatus node_status;

void AP_Periph_FW::send_node_status()
{
    node_status.uptime_sec = AP_HAL::millis() / 1000U;
    node_status.health = hal.util->get_soft_armed() && !healthy()
                         ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING
                         : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    node_status.mode     = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    node_status.sub_mode = 0;
    node_status.vendor_specific_status_code = vendor_status_bits();

    uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint32_t len = uavcan_protocol_NodeStatus_encode(&node_status, buf
#if CANARD_ENABLE_TAO_OPTION
        , !canfdout()
#endif
    );
    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buf, len);
}

// --- ESC telemetry ----------------------------------------------------
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    if (now_ms - last_esc_telem_ms < 1000U / MAX(g.esc_telem_rate.get(), 1)) {
        return;
    }
    last_esc_telem_ms = now_ms;

    for (uint8_t i = 0; i < ESC_N