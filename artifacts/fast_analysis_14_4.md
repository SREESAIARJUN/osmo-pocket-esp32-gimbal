# DroneCAN Node Heartbeat and ESC Telemetry for ArduPilot on STM32H743

"Cycle 14" doesn't match any DroneCAN or ArduPilot term I know. I've treated it as your own project or iteration label. If it implies specific constraints, such as a fixed rate budget or a particular ESC count, tell me and I'll adjust the numbers below.

---

## 1. Message Definitions (DSDL)

### 1.1 Node Heartbeat: `uavcan.protocol.NodeStatus`

| Field | Value |
|---|---|
| Data Type ID | **341** (broadcast) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits, 7 bytes, single CAN 2.0 frame |

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

uint3 sub_mode                      # reserved, set 0
uint16 vendor_specific_status_code
```

### 1.2 ESC Telemetry: `uavcan.equipment.esc.Status`

| Field | Value |
|---|---|
| Data Type ID | **1034** (broadcast) |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 110 bits, 14 bytes, multi-frame on classic CAN (3 frames incl. CRC) |

```
# uavcan/equipment/esc/1034.Status.uavcan
uint32 error_count          # cumulative, wraps
float16 voltage             # Volt
float16 current             # Ampere, may be negative (regen)
float16 temperature         # Kelvin
int18 rpm                   # negative = reverse
uint7 power_rating_pct      # 0..127 (%)
uint5 esc_index             # 0..31, matches RawCommand array index
```

### 1.3 Optional: `uavcan.equipment.esc.StatusExtended`

This is a DroneCAN extension (ID **1036**) that newer ArduPilot builds consume for input/output duty and motor temperature:

```
uint7  input_pct
uint7  output_pct
int9   motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```

Verify the field layout against your `DSDL/` checkout, since it is newer than the core set.

---

## 2. Publishing Implementation (AP_Periph style, libcanard + `dronecan_msgs.h`)

```cpp
// ---------- Heartbeat: call at 1 Hz ----------
static uint8_t node_status_tid;

void send_node_status(CanardInstance &ins, uint32_t now_ms)
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec  = now_ms / 1000U;
    msg.health      = periph_healthy() ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK
                                       : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING;
    msg.mode        = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    msg.sub_mode    = 0;
    msg.vendor_specific_status_code = hal.util->available_memory() & 0xFFFF; // example

    uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buf
#if CANARD_ENABLE_TAO_OPTION
                                                           , !canfdout()
#endif
                                                           );
    canardBroadcast(&ins,
                    UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                    UAVCAN_PROTOCOL_NODESTATUS_ID,
                    &node_status_tid,
                    CANARD_TRANSFER_PRIORITY_LOW,
                    buf, len);
}

// ---------- ESC telemetry: one message per ESC ----------
static uint8_t esc_status_tid;   // one transfer-ID counter per (data type, node)

void send_esc_status(CanardInstance &ins, uint8_t idx, const EscTelem &t)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.error_count      = t.error_count;
    pkt.voltage          = t.voltage_v;
    pkt.current          = t.current_a;
    pkt.temperature      = C_TO_KELVIN(t.temp_c);       // must be Kelvin
    pkt.rpm              = constrain_int32(t.rpm, -131072, 131071); // int18 range
    pkt.power_rating_pct = MIN(t.power_pct, 127);
    pkt.esc_index        = idx & 0x1F;

    uint8_t buf[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buf
#if CANARD_ENABLE_TAO_OPTION
                                                            , !canfdout()
#endif
                                                            );
    canardBroadcast(&ins,
                    UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                    UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                    &esc_status_tid,
                    CANARD_TRANSFER_PRIORITY_LOW,
                    buf, len);
}

// ---------- Scheduler ----------
void can_update(uint32_t now_ms)
{
    static uint32_t last_ns_ms, last_esc_ms;
    static uint8_t  next_esc;

    if (now_ms - last_ns_ms >= 1000) {           // NodeStatus: 1 Hz (spec max period)
        last_ns_ms = now_ms;
        send_node_status(canard, now_ms);
    }

    const uint32_t period = 1000U / (ESC_TELEM_RATE_HZ * NUM_ESCS);
    if (now_ms - last_esc_ms >= period) {        // round-robin to spread bus load
        last_esc_ms = now_ms;
        if (esc_telem_fresh(next_esc)) {
            send_esc_status(canard, next_esc, telem[next_esc]);
        }
        next_esc = (next_esc + 1) % NUM_ESCS;
    }
    processTx();                                  // drain canard queue to FDCAN
}
```

---

## 3. STM32H743 / ArduPilot Integration Notes

| Item | Recommendation |
|---|---|
| Peripheral | H743 has **FDCAN1/2**. Run them in classic CAN 2.0B mode at **1 Mbit/s** unless every node supports DroneCAN-FD. |
| hwdef | `PD0 CAN1_RX CAN1`, `PD1 CAN1_TX CAN1` (adjust to board); `define HAL_NUM_CAN