# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

I don't know what "Cycle 18" refers to in your project, so I've used it only as a label in the code. If it implies specific rates, ESC counts or vendor codes, substitute them in the configuration constants. Everything else follows the standard DroneCAN DSDL and ArduPilot `AP_Periph` conventions.

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus`

**Data Type ID:** 341 (broadcast)
**Payload:** 56 bits, 7 bytes, so it fits in a single classic CAN frame.
**Rate:** 1 Hz nominal. The spec bounds are a minimum period of 2 ms and a maximum of 1000 ms. Nodes are considered offline after 3 s without a NodeStatus.

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

uint3 sub_mode                      # reserved, set 0

uint16 vendor_specific_status_code
```

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

**Data Type ID:** 1034 (broadcast)
**Payload:** 110 bits, 14 bytes. This requires a multi-frame transfer on classic CAN (3 frames including the transfer CRC), or a single frame on CAN-FD.
**Rate:** Typically 10–50 Hz per ESC. Stagger the transmissions across ESC indices to avoid bus bursts.

### DSDL (`1034.Status.uavcan`)
```
uint32 error_count          # resets only on ESC power cycle
float16 voltage             # Volt
float16 current             # Ampere; negative = regen
float16 temperature         # Kelvin
int18  rpm                  # negative = reverse
uint7  power_rating_pct     # 0..100, 127 = unknown
uint5  esc_index            # 0..31
```

### Optional: `uavcan.equipment.esc.StatusExtended`
**Data Type ID:** 1036. This message is supported by ArduPilot's `AP_DroneCAN`.
```
uint7  input_pct            # commanded throttle %
uint7  output_pct           # actual output duty %
int9   motor_temperature_degC
uint9  motor_angle          # electrical/mech angle, 0..360
uint19 status_flags         # vendor-defined
uint5  esc_index
```

---

## 3. Publishing Implementation (AP_Periph style, STM32H743 FDCAN)

### Configuration constants
```cpp
// Cycle 18 configuration
#define C18_NODESTATUS_PERIOD_MS   1000
#define C18_ESC_STATUS_PERIOD_MS   20      // 50 Hz per ESC
#define C18_NUM_ESCS               4
#define C18_VENDOR_STATUS_CODE     0x0018
```

### Heartbeat publisher
```cpp
#include <dronecan_msgs.h>

void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec  = AP_HAL::millis() / 1000U;
    msg.health      = node_health;   // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    msg.mode        = node_mode;     // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    msg.sub_mode    = 0;
    msg.vendor_specific_status_code = C18_VENDOR_STATUS_CODE;

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buffer
#if CANARD_ENABLE_TAO_OPTION
        , !canfdout()
#endif
    );

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

### ESC status publisher
This version sends one ESC per call, round-robin, to stagger bus load.
```cpp
void AP_Periph_FW::esc_telem_update()
{
    static uint8_t next_esc;
    const uint32_t now = AP_HAL::millis();
    if (now - last_esc_telem_ms < C18_ESC_STATUS_PERIOD_MS / C18_NUM_ESCS) {
        return;
    }
    last_esc_telem_ms = now;

    const uint8_t i = next_esc;
    next_esc = (next_esc + 1) % C18_NUM_ESCS;

    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = i;
    pkt.error_count      = esc_telem.get_error_count(i);
    float v, c, t_degC, rpm;
    pkt.voltage          = esc_telem.get_voltage(i, v)  ? v : nanf("");
    pkt.current          = esc_telem.get_current(i, c)  ? c : nanf("");
    pkt.temperature      = esc_telem.get_temperature(i, t_degC)
                           ? t_degC + 273.15f : nanf("");   // Kelvin!
    pkt.rpm              = esc_telem.get_rpm(i, rpm)
                           ? constrain_int32(rpm, -131072, 131071) : 0;  // int18
    pkt.power_rating_pct = constrain_int16(esc_telem.get_throttle_pct(i), 0, 100);

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint16_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer
#if CANARD_ENABLE_TAO_OPTION
        , !canfdout()
#endif
    );

    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

---

## 4. Integration Notes for STM32H743

| Item | Recommendation |
|---|---|
| CAN peripheral | FDCAN1/2 (ChibiOS `hal_fdcan`). Use classic CAN at 1 Mbit/s unless every node supports FD. |
| Message RAM | The H7 shares 10 KB of FDCAN message RAM. Allocate enough TX FIFO elements for 3-frame ESC transfers × N ESCs. |
| Priority | Keep NodeStatus and ESC Status at LOW priority. ESC *commands* (RawCommand, ID 1030) use HIGH priority. |
| Float16 | `dronecan_dsdlc` handles the float32→float16 conversion. Saturate values to ±65504. |
| Temperature | The DSDL field is **Kelvin**. Mixing up °C and K is the most common telemetry bug. |
| Health mapping | Set WARNING on ESC over-temperature or desync, ERROR on lost ESC telemetry, and CRITICAL on an ESC failure that blocks arming. |
| Node ID | Use DNA (dynamic node allocation) or a fixed `CAN_NODE` parameter. Avoid ID 0 when publishing. |
| ArduPilot autopilot side | Set `CAN_D1_PROTOCOL=1` (DroneCAN) and `CAN_D1_UC_ESC_BM` to the ESC bitmask. Telemetry then appears in `ESC` log messages and the `ESC_TELEMETRY_x` MAVLink streams. |

If you can share what Cycle 18 specifies (ESC count, required rates, custom vendor fields), I can tailor the constants, or define a vendor-specific DSDL message in the 20000+ ID range for any extra data.