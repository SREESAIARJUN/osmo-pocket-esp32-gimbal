# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot (STM32H743)

I don't know what "Cycle 30" refers to in your project, such as a sprint, a release tag, or a scheduler slot. I've treated it as a label. If it means a specific publish rate or loop slot, adjust the timing constants in section 3.

This covers a node built on **AP_Periph** (for example, an H743 ESC or telemetry bridge board) publishing over **FDCAN1** at 1 Mbit/s. It uses the standard DroneCAN DSDL, so ArduPilot flight controllers consume the messages without custom parsing.

---

## 1. DSDL Message Definitions

### 1.1 Heartbeat: `uavcan.protocol.NodeStatus` (Data Type ID **341**)

```
# uavcan/protocol/341.NodeStatus.uavcan
# Abstract node status. Every node must publish this message.

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

- **Payload:** 56 bits = **7 bytes**. This is a single CAN frame.

### 1.2 ESC Telemetry: `uavcan.equipment.esc.Status` (Data Type ID **1034**)

```
# uavcan/equipment/esc/1034.Status.uavcan
# Generic ESC status. Unknown fields should be set to NAN.

uint32 error_count        # Resets on power cycle

float16 voltage           # Volt
float16 current           # Ampere. Can be negative (regen)
float16 temperature       # Kelvin

int18 rpm                 # Negative = reverse rotation

uint7 power_rating_pct    # Instant demand factor, 0..127 %

uint5 esc_index           # Index into esc.RawCommand array
```

- **Payload:** 110 bits, which rounds up to **14 bytes**. This is a multi-frame transfer (2 frames on classic CAN).

### 1.3 Optional: `uavcan.equipment.esc.StatusExtended` (Data Type ID **1036**)

This message is part of the newer DroneCAN DSDL set. Verify that your `modules/DroneCAN/DSDL` revision includes it before relying on it.

```
uint7  input_pct          # Commanded throttle 0..100
uint7  output_pct         # Actual output 0..100
int8   motor_temperature_degC
uint9  motor_angle        # 0..360 electrical/mechanical, ESC-defined
uint19 status_flags       # Manufacturer-specific bitfield
uint5  esc_index
```

---

## 2. Generated C Structures (libcanard / dronecan_dsdlc)

The structures below are produced automatically by the build into `dronecan_msgs.h`. They are shown here for reference only; do not hand-edit them.

```c
struct uavcan_protocol_NodeStatus {
    uint32_t uptime_sec;
    uint8_t  health;     // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    uint8_t  mode;       // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    uint8_t  sub_mode;
    uint16_t vendor_specific_status_code;
};
// UAVCAN_PROTOCOL_NODESTATUS_ID          341
// UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE    7

struct uavcan_equipment_esc_Status {
    uint32_t error_count;
    float    voltage;      // packed to float16 by encoder
    float    current;
    float    temperature;  // Kelvin
    int32_t  rpm;          // packed to int18 (range ±131071)
    uint8_t  power_rating_pct;
    uint8_t  esc_index;
};
// UAVCAN_EQUIPMENT_ESC_STATUS_ID         1034
// UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE   14
```

---

## 3. Publishing Implementation (AP_Periph style)

```cpp
// Tools/AP_Periph/esc_telem_pub.cpp
#include "AP_Periph.h"
#include <dronecan_msgs.h>

#define NODESTATUS_PERIOD_MS   1000   // 1 Hz (max allowed period)
#define ESC_STATUS_PERIOD_MS     20   // 50 Hz per ESC
#define KELVIN_OFFSET        273.15f

// ---------------- Heartbeat ----------------
void AP_Periph_FW::send_node_status(uint32_t now_ms)
{
    static uint32_t last_ms;
    if (now_ms - last_ms < NODESTATUS_PERIOD_MS) {
        return;
    }
    last_ms = now_ms;

    uavcan_protocol_NodeStatus msg {};
    msg.uptime_sec = now_ms / 1000U;
    msg.health     = node_health();        // OK/WARNING/ERROR/CRITICAL
    msg.mode       = node_mode();          // OPERATIONAL after init
    msg.sub_mode   = 0;
    msg.vendor_specific_status_code = vendor_status();  // e.g. fault bitmask

    uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&msg, buf
#if CANARD_ENABLE_TAO_OPTION
                                                           , !canfdout()
#endif
                                                           );

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buf, len);
}

// ---------------- ESC Telemetry ----------------
void AP_Periph_FW::send_esc_status(uint32_t now_ms)
{
    static uint32_t last_ms;
    if (now_ms - last_ms < ESC_STATUS_PERIOD_MS) {
        return;
    }
    last_ms = now_ms;

    for (uint8_t i = 0; i < num_escs; i++) {
        const EscTelem &t = esc_telem[i];          // filled by ESC driver/BLHeli telem
        uavcan_equipment_esc_Status msg {};

        msg.esc_index        = esc_index_offset + i;            // 0..31
        msg.error_count      = t.error_count;
        msg.voltage          = t.valid_volt ? t.voltage_v  : nanf("");
        msg.current          = t.valid_curr ? t.current_a  : nanf("");
        msg.temperature      = t.valid_temp ? t.temp_c + KELVIN_OFFSET : nanf("");
        msg.rpm              = constrain_int32(t.rpm, -131071, 131071);
        msg.power_rating_pct = MIN(t.power_pct, 127U);

        uint8_t buf[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
        const uint16_t len = uavcan_equipment_esc_Status_encode(&msg, buf
#if CANARD_ENABLE_TAO_OPTION
                                                                , !canfdout()
#endif
                                                                );

        canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                         UAVCAN_EQUIPMENT_ESC_STATUS_ID,