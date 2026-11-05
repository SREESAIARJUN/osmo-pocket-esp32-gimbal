# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

**Scope:** "Cycle 2" is read here as the second design iteration. Cycle 1 is assumed to have brought up FDCAN at 1 Mbps with dynamic node allocation. This cycle locks down the wire formats, publish rates, encoding rules and the publishing code paths. If your Cycle 2 means something else (for example, a specific scheduler cycle or a requirements baseline), tell me and I'll adjust.

---

## 1. Transport Baseline (STM32H743)

| Parameter | Value |
|---|---|
| Peripheral | FDCAN1 (FDCAN2 optional, redundant) |
| Bitrate | 1 Mbit/s classic CAN (CAN-FD is an option for Cycle 3) |
| Frame | 29-bit extended ID, 8-byte payload, last byte = tail byte |
| Stack | libcanard (ArduPilot fork) and dronecan_dsdlc-generated C structs |
| Node ID | Dynamic allocation, or `CAN_NODE` param on AP_Periph |

hwdef.dat excerpt (AP_Periph target):

```
MCU STM32H7xx STM32H743xx
PD0 CAN1_RX CAN1
PD1 CAN1_TX CAN1
define HAL_PERIPH_ENABLE_ESC_TELEM   # or the RC_OUT + ESC_TELEM path
define AP_PERIPH_ESC_TELEM_RATE_DEFAULT 50
```

---

## 2. Message 1 – Node Heartbeat: `uavcan.protocol.NodeStatus`

**Identity**

| Field | Value |
|---|---|
| Data Type ID | **341** (broadcast message) |
| Signature | `0x0F0868D0C1A7C6F1` |
| Payload | 56 bits = **7 bytes**, so it fits in a single frame and needs no transfer CRC |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (24) |
| Rate | **1 Hz**. Spec limits: max period 1000 ms, min 2 ms. Offline timeout is 3000 ms. |

**DSDL**

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

**Bit layout** (little-endian, packed):

```
Byte 0-3 : uptime_sec (uint32)
Byte 4   : [7:6] health | [5:3] mode | [2:0] sub_mode
Byte 5-6 : vendor_specific_status_code (uint16)
Byte 7   : tail byte (SOT=1, EOT=1, toggle=0, transfer_id)
```

**Project field policy for Cycle 2**

| Condition | health | mode |
|---|---|---|
| Boot, before first valid ESC data | OK | INITIALIZATION |
| Normal | OK | OPERATIONAL |
| Any ESC telemetry stale for more than 500 ms | WARNING | OPERATIONAL |
| ESC error_count increasing, or over-temperature | ERROR | OPERATIONAL |
| Bootloader / firmware update in progress | OK | SOFTWARE_UPDATE |

Proposed bit allocation for `vendor_specific_status_code`:

- bit 0–3: bitmask of ESCs with stale telemetry
- bit 4: over-temperature flag
- bit 5: under-voltage flag
- bit 15: watchdog reset occurred

**Publishing code** (AP_Periph style):

```cpp
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus ns {};
    ns.uptime_sec = AP_HAL::millis() / 1000U;
    ns.health     = node_health;      // set by monitor task
    ns.mode       = node_mode;
    ns.sub_mode   = 0;
    ns.vendor_specific_status_code = vendor_status_bits();

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint16_t len = uavcan_protocol_NodeStatus_encode(&ns, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

Call this from the 1 Hz branch of `AP_Periph_FW::can_update()`.

---

## 3. Message 2 – ESC Telemetry: `uavcan.equipment.esc.Status`

**Identity**

| Field | Value |
|---|---|
| Data Type ID | **1034** |
| Signature | `0xA9AF28AEA2FBB254` |
| Payload | 110 bits, so **14 bytes** |
| Transfer type | Multi-frame: 3 frames on classic CAN, with transfer CRC seeded by the signature |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (or MEDIUM if the FC uses it for RPM notch filtering) |
| Rate | **50 Hz per ESC** by default. Configurable 10–100 Hz. Publish ESCs round-robin, one `esc_index` per transfer. |

**DSDL**

```
uint32 error_count          # resets/CRC/desync count, monotonic
float16 voltage             # Volt
float16 current             # Ampere, negative = regen
float16 temperature         # KELVIN (not °C!)
int18 rpm                   # negative = reverse
uint7 power_rating_pct      # 0..127, 0..100 % of max
uint5 esc_index             # 0..31
```

**Bit layout**

```
bits   0- 31 : error_count
bits  32- 47 : voltage      (IEEE754 half)
bits  48- 63 : current      (IEEE754 half)
bits  64- 79 : temperature  (IEEE754 half, Kelvin)
bits  80- 97 : rpm          (int18, ±131071)
bits  98-104 : power_rating_pct
bits 105-109 : esc_index
```

Frame split on classic CAN: 2-byte CRC + 14 bytes = 16 bytes, which is 7 + 7 + 2 data bytes across 3 frames.

**Encoding rules (Cycle 2 requirements)**

1. Convert temperature to Kelvin: `degC + 273.15f`. Use `NaN` if no sensor is present.
2. Saturate RPM to ±131071 before encoding, since int18 wraps silently.
3. Clamp `power_rating_pct` to 0–100.
4. `esc_index` must match the FC's `CAN_Dx_ESC_OF` offset scheme. The FC adds its offset, so the periph publishes 0-based indices for its own outputs.
5. Do not publish an ESC whose data is older than 2× the publish period. Flag it in the NodeStatus vendor bits instead.

**Publishing code**

```cpp
void AP_Periph_FW::esc_telem_update()
{
    const uint32_t now = AP_HAL::millis();
    if (now - last_esc_telem_