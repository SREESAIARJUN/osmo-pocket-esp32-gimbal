# DroneCAN Heartbeat and ESC Telemetry Definitions for ArduPilot on STM32H743

"Cycle 23" isn't a standard DroneCAN or ArduPilot term, so I've treated it as your internal revision tag. It is recorded in the vendor status code below. Everything else follows upstream DSDL and the current ArduPilot `AP_DroneCAN` and `AP_Periph` conventions.

---

## 1. Message Definitions (DSDL)

### 1.1 Heartbeat: `uavcan.protocol.NodeStatus` (ID 341)

```
# 341.NodeStatus.uavcan
# Data type signature: 0x0F0868D0C1A7C6F1

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

uint3 sub_mode                      # Reserved, set to 0
uint16 vendor_specific_status_code
```

- **Size:** 56 bits (7 bytes). This fits in a single CAN 2.0 frame, so no transfer CRC is needed.
- **Rate:** 1 Hz.
- **Priority:** `CANARD_TRANSFER_PRIORITY_LOW` (24).

### 1.2 ESC Telemetry: `uavcan.equipment.esc.Status` (ID 1034)

```
# 1034.Status.uavcan
# Data type signature: 0xA9AF28AEA2FBB254

uint32  error_count          # Cumulative, resettable
float16 voltage              # Volt
float16 current              # Ampere; negative = regen
float16 temperature          # KELVIN (not °C)
int18   rpm                  # Negative = reverse
uint7   power_rating_pct     # 0..127 %, instant demand
uint5   esc_index            # 0..31
```

- **Size:** 110 bits, which rounds up to 14 bytes. This makes it a multi-frame transfer: 3 frames on classic CAN, with the transfer CRC seeded by the signature.
- **Rate:** 10 to 50 Hz per ESC (configurable).
- **Priority:** `CANARD_TRANSFER_PRIORITY_MEDIUM` or slightly higher.

### 1.3 Optional extension: `uavcan.equipment.esc.StatusExtended` (ID 1036)

ArduPilot 4.5+ supports this message for input/output percentage, motor temperature and status flags:

```
uint7  input_pct
uint7  output_pct
int16  motor_temperature_degC
uint9  motor_angle
uint19 status_flags
uint5  esc_index
```

---

## 2. Generated C Structs (dronecan_dsdlc output)

```c
// uavcan.protocol.NodeStatus.h
#define UAVCAN_PROTOCOL_NODESTATUS_ID         341
#define UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE  (0x0F0868D0C1A7C6F1ULL)
#define UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE   7

struct uavcan_protocol_NodeStatus {
    uint32_t uptime_sec;
    uint8_t  health;     // 2 bits
    uint8_t  mode;       // 3 bits
    uint8_t  sub_mode;   // 3 bits
    uint16_t vendor_specific_status_code;
};

// uavcan.equipment.esc.Status.h
#define UAVCAN_EQUIPMENT_ESC_STATUS_ID         1034
#define UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE  (0xA9AF28AEA2FBB254ULL)
#define UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE   14

struct uavcan_equipment_esc_Status {
    uint32_t error_count;
    float    voltage;           // encoded as float16
    float    current;           // encoded as float16
    float    temperature;       // encoded as float16, Kelvin
    int32_t  rpm;               // 18-bit signed
    uint8_t  power_rating_pct;  // 7 bits
    uint8_t  esc_index;         // 5 bits
};
```

---

## 3. Vendor Status Code Layout (Cycle 23 tag)

This is a suggested packing for the 16-bit `vendor_specific_status_code`:

| Bits  | Field          | Value / Meaning                        |
|-------|----------------|----------------------------------------|
| 15–11 | `cycle_rev`    | 23 (0b10111)                           |
| 10–8  | `can_bus_errs` | Saturating FDCAN error-warning counter |
| 7     | `esc_tlm_ok`   | 1 = all ESC telemetry fresh (<500 ms)  |
| 6     | `armed`        | Vehicle/ESC armed state                |
| 5–0   | `num_esc`      | Number of ESCs reporting               |

```c
#define CYCLE_REV 23u
static inline uint16_t pack_vssc(uint8_t can_errs, bool tlm_ok,
                                 bool armed, uint8_t n_esc) {
    return (uint16_t)((CYCLE_REV & 0x1F) << 11) |
           ((can_errs > 7 ? 7 : can_errs) << 8) |
           (tlm_ok ? 1u << 7 : 0) |
           (armed  ? 1u << 6 : 0) |
           (n_esc & 0x3F);
}
```

---

## 4. ArduPilot Publishing Implementation

### 4.1 Heartbeat in `AP_DroneCAN` (flight controller side)

```cpp
// AP_DroneCAN.h
Canard::Publisher<uavcan_protocol_NodeStatus> node_status{canard_iface};
uavcan_protocol_NodeStatus node_status_msg;
uint32_t _node_status_last_send_ms;

// AP_DroneCAN.cpp — called from loop()
void AP_DroneCAN::send_node_status(void)
{
    const uint32_t now = AP_HAL::millis();
    if (now - _node_status_last_send_ms < 1000) {
        return;
    }
    _node_status_last_send_ms = now;

    node_status_msg.uptime_sec = now / 1000U;
    node_status_msg.mode       = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    node_status_msg.sub_mode   = 0;
    node_status_msg.health     = hal.util->get_soft_armed() || _esc_tlm_healthy()
                                 ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK
                                 : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING;
    node_status_msg.vendor_specific_status_code =
        pack_vssc(_can_err_count(), _esc_tlm_healthy(),
                  hal.util->get_soft_armed(), _num_esc_reporting());

    node_status.broadcast(node_status_msg);
}
```

### 4.2 ESC Telemetry in `AP_Periph` (CAN ESC node on H743)

```cpp
// AP_Periph/esc_telem.cpp
#if AP_PERIPH_ESC_TELEM_ENABLED
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now = AP_HAL::millis();
    const uint32_t period_ms = 1000U / MAX(g.esc_telem_rate.get(), 1);
    if (now - last_esc_telem_update_ms < period_ms) {
        return;
    }
    last_esc_telem_update_ms = now;

    for (