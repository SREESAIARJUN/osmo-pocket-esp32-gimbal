# DroneCAN Node Heartbeat and ESC Telemetry for ArduPilot on STM32H743

I don't know what "Cycle 21" refers to. It may be an internal iteration or project label. The definitions below follow upstream ArduPilot and the DroneCAN DSDL. Adjust the rates or priorities if your cycle has its own requirements.

## 1. Message Overview

| Purpose | DSDL Type | Data Type ID | Payload | Transfer | Typical Rate |
|---|---|---|---|---|---|
| Heartbeat | `uavcan.protocol.NodeStatus` | 341 | 7 bytes (56 bits) | Single frame | 1 Hz (mandatory, period ≤ 1000 ms) |
| ESC telemetry | `uavcan.equipment.esc.Status` | 1034 | 14 bytes (110 bits) | Multi-frame on CAN 2.0, single on FD | 10–50 Hz per ESC |
| ESC extended (optional) | `uavcan.equipment.esc.StatusExtended` | 1036 | 7 bytes (56 bits) | Single frame | 10–50 Hz per ESC |

## 2. DSDL Definitions

### 2.1 `uavcan/protocol/341.NodeStatus.uavcan`

```
# Abstract node status information. Every node must publish this.

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

### 2.2 `uavcan/equipment/esc/1034.Status.uavcan`

```
# Generic ESC status.
uint32  error_count          # resets on ESC reboot
float16 voltage              # V
float16 current              # A, negative = regen
float16 temperature          # KELVIN (not °C)
int18   rpm                  # signed: negative = reverse
uint7   power_rating_pct     # 0..127 %, instantaneous demand vs. rated
uint5   esc_index            # 0..31, matches RawCommand array index
```

### 2.3 `uavcan/equipment/esc/1036.StatusExtended.uavcan` (DroneCAN extension)

```
uint7  input_pct               # commanded throttle, 0..100 %
uint7  output_pct              # actual output duty, 0..100 %
int9   motor_temperature_degC  # °C, -256..255
uint9  motor_angle             # electrical/mech angle, 0..360 deg
uint19 status_flags            # vendor/ESC-specific bitfield
uint5  esc_index
```

## 3. Generated C Structures (dronecan_dsdlc)

ArduPilot generates these from `modules/DroneCAN/DSDL` at build time:

```c
// dronecan_msgs.h (generated)
struct uavcan_protocol_NodeStatus {
    uint32_t uptime_sec;
    uint8_t  health;     // 2 bits
    uint8_t  mode;       // 3 bits
    uint8_t  sub_mode;   // 3 bits
    uint16_t vendor_specific_status_code;
};
#define UAVCAN_PROTOCOL_NODESTATUS_ID         341
#define UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE  (0x0F0868D0C1A7C6F1ULL)
#define UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE   7

struct uavcan_equipment_esc_Status {
    uint32_t error_count;
    float    voltage;
    float    current;
    float    temperature;
    int32_t  rpm;              // 18-bit signed on wire
    uint8_t  power_rating_pct;
    uint8_t  esc_index;
};
#define UAVCAN_EQUIPMENT_ESC_STATUS_ID         1034
#define UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE  (0xA9AF28AEA2FBB254ULL)
#define UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE   14
```

Always use the macros from the generated header. Never hand-copy the signatures, because a wrong signature corrupts the multi-frame CRC.

## 4. Publishing Implementation (AP_Periph style)

This applies to an STM32H743 node acting as a CAN ESC or telemetry bridge.

### 4.1 Heartbeat

```cpp
// AP_Periph/can.cpp
void AP_Periph_FW::send_node_status(void)
{
    uavcan_protocol_NodeStatus node_status{};
    node_status.uptime_sec = AP_HAL::millis() / 1000U;
    node_status.health     = health_from_system_state();   // OK/WARNING/ERROR/CRITICAL
    node_status.mode       = in_bootloader_or_update()
                               ? UAVCAN_PROTOCOL_NODESTATUS_MODE_SOFTWARE_UPDATE
                               : UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    node_status.sub_mode   = 0;
    node_status.vendor_specific_status_code =
        hal.util->available_memory() >> 4;   // example: free RAM indicator

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&node_status, buffer
#if CANARD_ENABLE_TAO_OPTION
                                                           , !canfdout()
#endif
                                                           );
    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// called from the 1 Hz section of the main loop
if (now - last_1Hz_ms >= 1000) {
    last_1Hz_ms = now;
    send_node_status();
}
```

### 4.2 ESC Telemetry

```cpp
#if AP_PERIPH_ESC_TELEM_ENABLED  // macro name varies by branch
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now_ms = AP_HAL::millis();
    // ESC_TELEM_RATE param (Hz), default 50
    if (g.esc_telem_rate <= 0 ||
        now_ms - last_esc_telem_ms < 1000U / g.esc_telem_rate) {
        return;
    }
    last_esc_telem_ms = now_ms;

    for (uint8_t i = 0; i < ESC_NUMBERS; i++) {
        const uint8_t idx = g.esc_number[i];          // ESC_NUMBER param mapping
        if (!esc_telem.is_telemetry_active(1U << i)) {
            continue;
        }

        uavcan_equipment_esc_Status pkt{};
        float v, a, rpm;
        int16_t t_cdeg;

        pkt.esc_index   = idx;
        pkt.voltage     = esc_telem.get_