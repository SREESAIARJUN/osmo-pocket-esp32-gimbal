# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot on STM32H743

"Cycle 20" isn't a standard DroneCAN or ArduPilot term, so I've assumed it's your internal iteration label. If it implies specific constraints, such as a 20 ms loop, a particular ESC count or a CAN FD requirement, tell me and I'll adjust the rates and scheduling.

Neither message is custom. Both are standard DSDL types that already ship in ArduPilot's `modules/DroneCAN/DSDL`. Don't redefine them, because their data type signatures must match the flight controller's.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

**File:** `uavcan/protocol/341.NodeStatus.uavcan`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits = **7 bytes** (single frame, no transfer CRC) |
| Rate | **1 Hz** nominal. The spec requires at least every 1000 ms and allows at most every 2 ms. |

```
uint16 MAX_BROADCASTING_PERIOD_MS = 1000
uint16 MIN_BROADCASTING_PERIOD_MS = 2
uint16 OFFLINE_TIMEOUT_MS = 3000

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

uint3 sub_mode                    # reserved, set 0
uint16 vendor_specific_status_code
```

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

**File:** `uavcan/equipment/esc/1034.Status.uavcan`

| Property | Value |
|---|---|
| Data Type ID | **1034** (broadcast) |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 110 bits = **14 bytes** |
| Framing on CAN 2.0 | Multi-frame: 14 bytes plus a 2-byte transfer CRC = 16 bytes, sent as **3 frames** |
| Framing on CAN FD | Single frame on the H743 FDCAN peripheral |
| Rate | Typically 10–50 Hz per ESC. In AP_Periph this is set by `ESC_TELEM_RATE`. |

```
uint32 error_count        # resets on ESC reboot
float16 voltage           # Volts
float16 current           # Amps; negative means regen
float16 temperature       # KELVIN (not °C)
int18 rpm                 # negative means reverse rotation
uint7 power_rating_pct    # 0..127, % of max power
uint5 esc_index           # 0..31, must match the FC's ESC/motor index
```

**Optional extension:** `uavcan.equipment.esc.StatusExtended` (ID **1036**) adds:
- input_pct,
- output_pct,
- motor_temperature_degC,
- motor_angle,
- status_flags.

ArduPilot's `AP_DroneCAN` consumes it if you need motor temperature.

---

## 3. Publishing Code (AP_Periph style, using `dronecan_dsdlc` generated headers)

```cpp
#include <dronecan_msgs.h>   // generated: uavcan.protocol.NodeStatus, uavcan.equipment.esc.Status

// ---------- Heartbeat @ 1 Hz ----------
static void send_node_status(void)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = AP_HAL::millis() / 1000U;
    msg.health     = periph_healthy() ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK
                                      : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING;
    msg.mode       = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = hal.util->available_memory() / 1024U; // example usage

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                           , !periph.canfdout()
#endif
                                                           );
    periph.canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                            UAVCAN_PROTOCOL_NODESTATUS_ID,
                            CANARD_TRANSFER_PRIORITY_LOW,
                            buffer, len);
}

// ---------- ESC telemetry @ ESC_TELEM_RATE, one transfer per ESC ----------
static void send_esc_status(uint8_t esc_index, const EscTelem &t)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.error_count      = t.error_count;
    pkt.voltage          = t.voltage_v;
    pkt.current          = t.current_a;
    pkt.temperature      = C_TO_KELVIN(t.temp_degC);         // spec requires Kelvin
    pkt.rpm              = constrain_int32(t.rpm, -131072, 131071); // int18 range
    pkt.power_rating_pct = constrain_int16(t.power_pct, 0, 127);
    pkt.esc_index        = esc_index & 0x1F;

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                            , !periph.canfdout()
#endif
                                                            );
    periph.canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                            UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                            CANARD_TRANSFER_PRIORITY_LOW,
                            buffer, len);
}

// ---------- Scheduler hook ----------
void periph_can_update(void)
{
    const uint32_t now = AP_HAL::millis();

    static uint32_t last_ns_ms;
    if (now - last_ns_ms >= 1000) {
        last_ns_ms = now;
        send_node_status();
    }

    static uint32_t last_esc_ms;
    const uint32_t esc_period_ms = 1000U / MAX(1, g.esc_telem_rate);
    if (now - last_esc_ms >= esc_period_ms) {
        last_esc_ms = now;
        for (uint8_t i = 0; i < num_escs; i++) {
            if (esc_telem_fresh(i)) {
                send_esc_status(i, esc_telem[i]);
            }
        }
    }
}
```

In newer ArduPilot (4.4 and later), you can use the `Canard::Publisher<uavcan_protocol_NodeStatus>` wrapper instead of raw `canard_broadcast`. The message definitions are the same either way.

---

## 4. Implementation Notes for the STM32H743

- **Tail-array optimisation (TAO):** pass `!canfdout()` so encoding matches the bus mode. Neither of these messages has a tail array, but keep the convention.
- **Bus load:** at 1 Mbit/s CAN 2.0, ESC Status costs about 3 frames × ~130 bits ≈ 400 µs of bus time per ESC per transfer. For example, 8 ESCs at 50 Hz uses roughly 16% of the bus. Lower the rate, or enable CAN FD, if the bus is shared with GPS or compass nodes