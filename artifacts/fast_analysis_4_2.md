# DroneCAN Node Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

"Cycle 4" isn't a standard DroneCAN or ArduPilot term. This answer assumes it is your project's iteration label and gives a complete, self-contained specification. Tell me if it refers to something specific, such as a prior design revision.

---

## 1. Scope and Architecture

| Role | Firmware | Function |
|---|---|---|
| **Publisher** | AP_Periph on STM32H743 (ESC/peripheral node) | Broadcasts `NodeStatus` and ESC telemetry |
| **Subscriber** | ArduPilot flight controller (`AP_DroneCAN`) | Consumes telemetry into `AP_ESC_Telem` |

- **Transport:** H743 FDCAN1/FDCAN2.
- **Frame mode:** Classic CAN 2.0 at 1 Mbit/s by default. CAN-FD is optional (`CAN_Px_FDBITRATE`).
- **Encoding:** DroneCAN v0 (UAVCAN v0 lineage), libcanard with dronecan_dsdlc-generated C headers.

---

## 2. Message Definitions (DSDL)

### 2.1 `uavcan.protocol.NodeStatus`: heartbeat
**Data Type ID 341.** The payload is 56 bits (7 bytes), so it fits in a single classic CAN frame.

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

uint3 sub_mode                      # reserved, set 0
uint16 vendor_specific_status_code
```

### 2.2 `uavcan.equipment.esc.Status`: primary ESC telemetry
**Data Type ID 1034.** The payload is 110 bits (14 bytes). On classic CAN this becomes a 2-frame multi-frame transfer. On CAN-FD it fits in a single frame.

```
# 1034.Status.uavcan
uint32  error_count          # cumulative error counter
float16 voltage              # Volts
float16 current              # Amps (negative = regen)
float16 temperature          # KELVIN (convert from °C: +273.15)
int18   rpm                  # signed, direction-aware
uint7   power_rating_pct     # 0..127, % of max power
uint5   esc_index            # 0..31
```

### 2.3 `uavcan.equipment.esc.StatusExtended`: optional extended telemetry
**Data Type ID 1036.** The payload is 56 bits (7 bytes), a single frame. This message is supported in recent ArduPilot builds.

```
# 1036.StatusExtended.uavcan
uint7  input_pct             # demanded throttle 0..100
uint7  output_pct            # actual output duty 0..100
int9   motor_temperature_degC
uint9  motor_angle           # 0..360 deg, if sensored
uint19 status_flags          # vendor-defined fault bits
uint5  esc_index
```

---

## 3. Publishing Policy

| Message | Rate | Priority | Frames (CAN 2.0 / FD) | Notes |
|---|---|---|---|---|
| NodeStatus | **1 Hz** (fixed) | `CANARD_TRANSFER_PRIORITY_LOW` | 1 / 1 | Required. The FC marks the node offline after 3 s of silence. |
| esc.Status | 10–50 Hz per ESC (param, e.g. `ESC_TELEM_RATE`) | `LOW` (or `MEDIUM`) | 2 / 1 | One transfer per `esc_index`, staggered. |
| esc.StatusExtended | ≤ 10 Hz | `LOW` | 1 / 1 | Optional. |

**Bus load check (classic CAN, 1 Mbit/s, 4 ESCs at 50 Hz):**
- Each frame is about 130 bits including stuffing.
- 4 × 50 × 2 frames × 130 bits ≈ 52 kbit/s, which is about 5% of the bus.

Keep total load below 30–40%. Never schedule ESC telemetry above the priority of `esc.RawCommand` (1030), which arrives from the FC at high priority.

---

## 4. Publisher Implementation (AP_Periph style, libcanard + dronecan_dsdlc)

### 4.1 Heartbeat

```cpp
#include <dronecan_msgs.h>

void AP_Periph_FW::node_status_send(void)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = node_health;     // HEALTH_OK unless faults latched
    msg.mode       = node_mode;       // MODE_OPERATIONAL after init
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = vendor_status; // e.g. hal.util->available_memory() or fault code

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint32_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// In the 1 Hz section of the main loop:
if (now - last_1Hz_ms >= 1000) {
    last_1Hz_ms = now;
    node_status_send();
}
```

**Health mapping recommendation:**

| Condition | `health` |
|---|---|
| Normal | `OK` |
| Over-temperature warning, voltage sag | `WARNING` |
| Desync, ESC fault, telemetry loss | `ERROR` |
| Overcurrent shutdown, hardware fault | `CRITICAL` |

### 4.2 ESC Status

```cpp
void AP_Periph_FW::esc_telem_send(uint8_t esc_index, const ESCTelemData &t)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = esc_index;                     // 0..31
    pkt.voltage          = t.voltage_v;
    pkt.current          = t.current_a;
    pkt.temperature      = C_TO_KELVIN(t.temp_degC);      // MUST be Kelvin
    pkt.rpm              = constrain_int32(t.erpm / pole_pairs, -131072, 131071); // int18
    pkt.power_rating_pct = constrain_int16(t.power_pct, 0, 127);
    pkt.error_count      = t.error_count;

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint32_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer, !canfdout());

    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     