# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

I don't know what "Cycle 17" refers to. It could be an internal sprint, a revision, or a test cycle. This answer covers the standard DroneCAN definitions as ArduPilot uses them. Tell me what Cycle 17 changes, such as rates, a custom vendor status code, or extended telemetry, and I'll adapt it.

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** |
| Signature | `0x0F0868D0C1A7C6F1` |
| Transfer | Broadcast, single frame (7 bytes) |
| Priority | Low (`CANARD_TRANSFER_PRIORITY_LOW`) |
| Rate | 1 Hz typical. The spec allows a minimum period of 2 ms and a maximum of 1000 ms. |
| Offline timeout | 3000 ms |

### DSDL (`341.NodeStatus.uavcan`)
```
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
uint16 vendor_specific_status_code  # free for project use
```
The payload totals 56 bits (7 bytes), so it fits in one classic CAN frame together with the tail byte.

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** |
| Signature | `0xA9AF28AEA2FBB254` |
| Transfer | Broadcast, **multi-frame** on classic CAN (14 bytes, so transfer CRC is required) |
| Priority | Typically `CANARD_TRANSFER_PRIORITY_LOW` or medium-low |
| Rate | 10–50 Hz per ESC. ArduPilot's `ESC_TELEM_RATE` param in AP_Periph defaults to 50 Hz. |

### DSDL (`1034.Status.uavcan`)
```
uint32 error_count        # resets on power cycle
float16 voltage           # Volt
float16 current           # Ampere, negative = regen
float16 temperature       # Kelvin
int18 rpm                 # negative = reverse
uint7 power_rating_pct    # 0..127 % of max continuous power
uint5 esc_index           # 0..31, matches RawCommand index
```
The payload totals 110 bits, which is 14 bytes after padding.

### Optional: `uavcan.equipment.esc.StatusExtended` (DTID 1036)
ArduPilot supports this message for additional data:
```
uint7  input_pct          # commanded input, %
uint7  output_pct         # actual output/duty, %
int16  motor_temperature_degC
uint9  motor_angle        # electrical/mech angle, if available
uint19 status_flags       # vendor-defined
uint5  esc_index
```

---

## 3. Publishing Implementation (AP_Periph style, libcanard + dronecan_dsdlc)

```cpp
#include <dronecan_msgs.h>

// ---------- Heartbeat (call at 1 Hz) ----------
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus ns {};
    ns.uptime_sec  = AP_HAL::millis() / 1000U;
    ns.health      = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    ns.mode        = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    ns.sub_mode    = 0;
    ns.vendor_specific_status_code = vendor_status;   // e.g. free heap / fault bits

    uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&ns, buf
#if CANARD_ENABLE_TAO_OPTION
        , !canfdout()
#endif
    );

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buf, len);
}

// ---------- ESC telemetry (call at ESC_TELEM_RATE, per ESC) ----------
void AP_Periph_FW::send_esc_status(uint8_t idx, const EscTelem &t)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = idx;
    pkt.error_count      = t.error_count;
    pkt.voltage          = t.voltage_v;
    pkt.current          = t.current_a;
    pkt.temperature      = C_TO_KELVIN(t.temp_c);
    pkt.rpm              = constrain_int32(t.rpm, -131072, 131071); // int18 range
    pkt.power_rating_pct = constrain_int16(t.power_pct, 0, 127);

    uint8_t buf[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buf
#if CANARD_ENABLE_TAO_OPTION
        , !canfdout()
#endif
    );

    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buf, len);
}
```

### Scheduler hookup
```cpp
void AP_Periph_FW::can_update()
{
    const uint32_t now = AP_HAL::millis();
    if (now - last_node_status_ms >= 1000) {
        last_node_status_ms = now;
        send_node_status();
    }
    const uint32_t telem_period = 1000U / MAX(1, g.esc_telem_rate.get());
    if (now - last_esc_telem_ms >= telem_period) {
        last_esc_telem_ms = now;
        for (uint8_t i = 0; i < num_escs; i++) {
            send_esc_status(esc_base_index + i, telem[i]);
        }
    }
    processTx(); processRx();
}
```

---

## 4. STM32H743-Specific Notes

- **Controller:** The H743 uses FDCAN1/FDCAN2. ArduPilot's ChibiOS HAL (`CANFDIface`) handles these. Run classic CAN at 1 Mbit/s unless the whole bus supports FD.
- **Bus load:** Each ESC Status message takes 2 frames on classic CAN. For 8 ESCs at 50 Hz that is 800 frames/s, roughly 10% of a 1 Mbit/s bus. Lower `ESC_TELEM_RATE` or move to CAN-FD if the bus is shared with GPS or compass traffic.
- **Message RAM:** FDCAN message RAM (`0x4000AC00`) is non-cached. Don't place encode buffers in DTCM expecting DMA; libcanard copies them into TX mailboxes.
- **hwdef:** Enable the relevant options, e.g. `define HAL_PERIPH_ENABLE_ESC_TELEM` or `HAL_PERIPH_ENABLE_RC_OUT` with telemetry, and set `CAN_D1_UC_ESC_BM` / `CAN_D1_UC_ESC_