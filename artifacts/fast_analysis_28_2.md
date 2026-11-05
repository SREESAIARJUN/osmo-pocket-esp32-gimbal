# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

> **Note on "Cycle 28":** I don't recognize this as a public ArduPilot or DroneCAN identifier. I've treated it as your internal iteration label. If it refers to a specific ArduPilot release branch or an internal spec revision, tell me and I'll align the details.

The definitions below follow the standard DroneCAN v0 (UAVCAN v0) DSDL used by ArduPilot through `libcanard` and `dronecan_msgs.h`. The publishing context is **AP_Periph** running on an STM32H743 node, such as a CAN ESC or ESC-telemetry bridge.

---

## 1. Node Heartbeat: `uavcan.protocol.NodeStatus`

| Property | Value |
|---|---|
| Data Type ID | **341** (broadcast message) |
| Data Type Signature | `0x0F0868D0C1A7C6F1` |
| Max payload | 7 bytes (56 bits), so it fits in a single classic CAN frame |
| Publish rate | **1 Hz** nominal (allowed range: every 2 ms to 1000 ms) |
| Offline timeout | 3000 ms (peers mark the node offline after this) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (ArduPilot convention) |

### DSDL (`341.NodeStatus.uavcan`)

```
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

### Bit layout (little-endian, packed)

| Bits | Field |
|---|---|
| 0–31 | `uptime_sec` |
| 32–33 | `health` |
| 34–36 | `mode` |
| 37–39 | `sub_mode` |
| 40–55 | `vendor_specific_status_code` |

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

| Property | Value |
|---|---|
| Data Type ID | **1034** |
| Data Type Signature | `0xA9AF28AEA2FBB254` |
| Max payload | 14 bytes (110 bits), so it is multi-frame on classic CAN and single-frame on CAN-FD |
| Publish rate | Typically 10–50 Hz per ESC. In AP_Periph this is set by `ESC_TELEM_RATE`. |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` |

### DSDL (`1034.Status.uavcan`)

```
uint32  error_count        # Resets on power cycle
float16 voltage            # Volts
float16 current            # Amps (negative = regenerative braking)
float16 temperature        # KELVIN
int18   rpm                # Negative = reverse rotation
uint7   power_rating_pct   # 0..127 (% of max power)
uint5   esc_index          # 0..31, matches RawCommand array index
```

Unknown float values must be sent as `NaN`.

### Optional extension: `uavcan.equipment.esc.StatusExtended` (DTID 1036)

ArduPilot also supports this message for input/output demand, motor temperature, and status flags:

```
uint7  input_pct
uint7  output_pct
int9   motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```

---

## 3. ArduPilot / AP_Periph Publishing Implementation

### Heartbeat (AP_Periph, `can.cpp` style)

```cpp
#include <dronecan_msgs.h>

void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus node_status {};
    node_status.uptime_sec = AP_HAL::millis() / 1000U;
    node_status.health     = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    node_status.mode       = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    node_status.sub_mode   = 0;
    node_status.vendor_specific_status_code =
        hal.util->available_memory() & 0xFFFF;   // optional diagnostic

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint32_t len = uavcan_protocol_NodeStatus_encode(&node_status, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                         , !canfdout()
#endif
                                                         );
    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// In the 1 Hz section of the main loop:
// if (now - last_1Hz_ms >= 1000) { last_1Hz_ms = now; send_node_status(); }
```

### ESC telemetry

```cpp
void AP_Periph_FW::send_esc_telemetry(uint8_t esc_index)
{
    const AP_ESC_Telem &telem = esc_telem;
    uavcan_equipment_esc_Status pkt {};

    float f;
    pkt.esc_index        = esc_index & 0x1F;
    pkt.voltage          = telem.get_voltage(esc_index, f)     ? f : nanf("");
    pkt.current          = telem.get_current(esc_index, f)     ? f : nanf("");
    int16_t temp_cdeg;
    pkt.temperature      = telem.get_temperature(esc_index, temp_cdeg)
                           ? C_TO_KELVIN(temp_cdeg * 0.01f) : nanf("");
    pkt.rpm              = telem.get_rpm(esc_index, f)
                           ? constrain_float(f, -131072, 131071) : 0;  // int18 range
    pkt.power_rating_pct = 0;            // fill if the ESC reports it (0..127)
    pkt.error_count      = telem.get_error_count(esc_index);   // adapt to your source

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

The accessor names in `AP_ESC_Telem` vary by branch, so check them against your tree.

### Flight-controller side (receiving)

`AP_DroneCAN` subscribes automatically:

```cpp
Canard::ObjCallback<AP_DroneCAN, uavcan_equipment_esc_Status> esc_status_cb{this, &AP_DroneCAN::handle_ESC_status};
```

The callback feeds `AP_ESC_Telem::update_telem_data()`. The flight controller maps `esc_index` against `CAN_D1_UC_ESC_OF` (the 