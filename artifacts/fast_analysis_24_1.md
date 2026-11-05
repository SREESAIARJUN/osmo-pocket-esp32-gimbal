# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot (STM32H743)

> **About "Cycle 24":** I don't recognize this as an ArduPilot, DroneCAN, or STM32 identifier. It may be an internal project, sprint, or release label. The definitions below follow the standard DroneCAN DSDL and current ArduPilot (`AP_DroneCAN` / `AP_Periph`) conventions. If "Cycle 24" implies a specific firmware tag or custom message set, compare these against that branch.

---

## 1. Message Definitions (DSDL)

### 1.1 Node Heartbeat: `uavcan.protocol.NodeStatus` (Data Type ID 341)

```
# 341.NodeStatus.uavcan
# Mandatory heartbeat. Every DroneCAN node must publish this.

uint16 MAX_BROADCASTING_PERIOD_MS = 1000
uint16 MIN_BROADCASTING_PERIOD_MS = 2
uint16 OFFLINE_TIMEOUT_MS         = 3000

uint32 uptime_sec                 # Seconds since boot

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

- **Payload:** 7 bytes, so it fits in a single classic CAN frame.
- **Priority:** low.
- **Rate:** 1 Hz.

### 1.2 ESC Telemetry: `uavcan.equipment.esc.Status` (Data Type ID 1034)

```
# 1034.Status.uavcan
# Generic ESC status. Published per ESC, ~10-50 Hz typical.

uint32  error_count        # Cumulative resettable error counter
float16 voltage            # Volts
float16 current            # Amperes, negative = regen
float16 temperature        # KELVIN
int18   rpm                # Negative = reverse
uint7   power_rating_pct   # 0..127 %, instantaneous demand
uint5   esc_index          # 0..31
```

- **Payload:** 14 bytes, which means a multi-frame transfer on classic CAN and a single frame on CAN-FD.

### 1.3 Optional: `uavcan.equipment.esc.StatusExtended` (ID 1036)

DroneCAN added this message for richer ESC data. Verify the exact field widths against the `DSDL` submodule in your ArduPilot tree:

```
uint7  input_pct
uint7  output_pct
int16  motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```

---

## 2. Publishing on the Node (AP_Periph, STM32H743)

### 2.1 Heartbeat (1 Hz task)

```cpp
// AP_Periph/can.cpp style
#include <dronecan_msgs.h>

void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus pkt {};
    pkt.uptime_sec = AP_HAL::millis() / 1000U;
    pkt.health     = node_health;     // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    pkt.mode       = node_mode;       // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    pkt.sub_mode   = 0;
    pkt.vendor_specific_status_code = vendor_status; // e.g. free stack/heap or error bits

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&pkt, buffer
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

### 2.2 ESC Telemetry (per ESC, rate-limited)

```cpp
void AP_Periph_FW::send_esc_status(uint8_t idx,
                                   float volt, float amp,
                                   float temp_degC, int32_t rpm,
                                   uint8_t power_pct, uint32_t errors)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = idx & 0x1F;
    pkt.voltage          = volt;                    // generated code packs to float16
    pkt.current          = amp;
    pkt.temperature      = temp_degC + 273.15f;     // C -> K (common bug source)
    pkt.rpm              = constrain_int32(rpm, -131072, 131071); // int18 saturation
    pkt.power_rating_pct = MIN(power_pct, 127);
    pkt.error_count      = errors;

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

### 2.3 Rate Control

```cpp
// in AP_Periph_FW::update()
const uint32_t now = AP_HAL::millis();
if (now - last_node_status_ms >= 1000) {
    last_node_status_ms = now;
    send_node_status();
}
const uint16_t esc_period_ms = 1000 / MAX(g.esc_telem_rate.get(), 1);
if (now - last_esc_telem_ms >= esc_period_ms) {
    last_esc_telem_ms = now;
    for (uint8_t i = 0; i < num_escs; i++) {
        send_esc_status(i, /* telemetry from AP_ESC_Telem / serial ESC */ ...);
    }
}
```

---

## 3. Flight Controller Side (`AP_DroneCAN`, H743 autopilot)

The flight controller publishes its own NodeStatus and subscribes to ESC Status:

```cpp
// AP_DroneCAN.h
Canard::Publisher<uavcan_protocol_NodeStatus> node_status{canard_iface};
Canard::ObjCallback<AP_DroneCAN, uavcan_equipment_esc_Status> esc_status_cb{this, &AP_DroneCAN::handle_ESC_status};
Canard::Subscriber<uavcan_equipment_esc_Status> esc_status_listener{esc_status_cb, _driver_index};

// AP_DroneCAN.cpp
void AP_DroneCAN::send_node_status()
{
    const uint32_t now = AP_HAL::millis();
    if (now - _node_status_last_send_ms < 1000) return;
    _node_status_last_send_ms = now;
    node_status_msg.uptime_sec = now / 1000;
    node_status.broadcast(node_status_msg);
}

void AP_DroneCAN::handle_ESC_status(const CanardRxTransfer& transfer,
                                    const uavcan_equipment_esc_Status& msg)
{
    const uint8_t esc_index = msg.esc_index + _esc_offset;   