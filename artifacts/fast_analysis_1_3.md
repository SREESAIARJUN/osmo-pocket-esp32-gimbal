# DroneCAN Message Definitions: Node Heartbeat and ESC Telemetry
**Target:** ArduPilot (AP_Periph node or flight controller) on STM32H743 (FDCAN1/FDCAN2)
**Revision:** Cycle 1 (baseline definitions, classic CAN 2.0B, 1 Mbit/s)

---

## 1. Scope

Cycle 1 covers two broadcast messages:

| Function | DSDL Type | Data Type ID | Transfer | Direction |
|---|---|---|---|---|
| Node heartbeat | `uavcan.protocol.NodeStatus` | **341** | Single-frame, 7 bytes | Every node → bus |
| ESC telemetry | `uavcan.equipment.esc.Status` | **1034** | Multi-frame, 14 bytes | ESC node → FC |

Out of scope for Cycle 1: `GetNodeInfo` (service 1), dynamic node allocation (ID 1), `esc.StatusExtended` (ID 1036), and CAN-FD framing.

---

## 2. Node Heartbeat: `uavcan.protocol.NodeStatus`

### 2.1 DSDL definition (`341.NodeStatus.uavcan`)

```
# Abstract node status information.
# Every UAVCAN node must publish this message periodically.

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

### 2.2 Wire layout (56 bits = 7 bytes, single frame)

| Bit offset | Width | Field |
|---|---|---|
| 0 | 32 | `uptime_sec` (little-endian) |
| 32 | 2 | `health` |
| 34 | 3 | `mode` |
| 37 | 3 | `sub_mode` |
| 40 | 16 | `vendor_specific_status_code` |
| — | 8 | Tail byte (SOT=1, EOT=1, toggle=0, transfer ID) |

**Data type signature:** `0x0F0868D0C1A7C6F1`. It is not used for single-frame CRC, but the encoder still requires it.

### 2.3 Publishing rules (Cycle 1)

| Parameter | Value |
|---|---|
| Rate | **1 Hz** (fixed; the spec maximum period is 1000 ms) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (24) |
| `uptime_sec` | `AP_HAL::millis() / 1000` |
| `mode` | `INITIALIZATION` until sensors/ESC are ready, then `OPERATIONAL`; `SOFTWARE_UPDATE` in bootloader |
| `health` | Mapped from local fault state (see 2.4) |
| `vendor_specific_status_code` | Bitmask of active faults (see 2.4) |

### 2.4 Health and vendor code mapping

| Condition | `health` | VSSC bit |
|---|---|---|
| Nominal | OK | — |
| ESC temperature > warning threshold | WARNING | bit 0 |
| Telemetry UART/DShot timeout | WARNING | bit 1 |
| ESC desync / overcurrent latched | ERROR | bit 2 |
| Watchdog reset occurred this boot | WARNING | bit 3 |
| Supply voltage out of range | ERROR | bit 4 |
| Hard fault / unrecoverable | CRITICAL | bit 15 |

---

## 3. ESC Telemetry: `uavcan.equipment.esc.Status`

### 3.1 DSDL definition (`1034.Status.uavcan`)

```
# Generic ESC status.
# Unknown fields should be set to NAN.

uint32 error_count          # Resets when the motor restarts

float16 voltage             # Volt
float16 current             # Ampere. Can be negative in case of a regenerative braking.
float16 temperature         # Kelvin

int18 rpm                   # Negative value indicates reverse rotation

uint7 power_rating_pct      # Instant demand factor in percent (percent of maximum power); range 0% to 127%.

uint5 esc_index
```

### 3.2 Wire layout (110 bits, padded to 14 bytes, multi-frame)

| Bit offset | Width | Field | Encoding |
|---|---|---|---|
| 0 | 32 | `error_count` | uint32 LE |
| 32 | 16 | `voltage` | IEEE 754 binary16 |
| 48 | 16 | `current` | IEEE 754 binary16 |
| 64 | 16 | `temperature` | IEEE 754 binary16, **Kelvin** |
| 80 | 18 | `rpm` | int18, range ±131071 |
| 98 | 7 | `power_rating_pct` | 0–127 |
| 105 | 5 | `esc_index` | 0–31 |

**Data type signature:** `0xA9AF28AEA2FBB254`. This one is required, because it seeds the transfer CRC.

**Framing:** 2-byte transfer CRC plus 14 payload bytes gives 16 bytes. At 7 payload bytes per frame, that is **3 CAN frames per transfer**.

### 3.3 Publishing rules (Cycle 1)

| Parameter | Value |
|---|---|
| Rate | **50 Hz per ESC** (parameter `ESC_TELEM_RATE`, range 1–200 Hz) |
| Priority | `CANARD_TRANSFER_PRIORITY_LOW` (24). Must remain lower priority than `esc.RawCommand` |
| `esc_index` | Equals the FC's motor output channel index minus 1 (parameter `ESC_INDEX`) |
| Unknown fields | `NAN` for floats, `0` for integers |
| `temperature` | `temp_degC + 273.15f` |
| Multi-ESC node | One transfer per ESC, staggered evenly within the period to avoid bursts |

### 3.4 Bus load budget (1 Mbit/s, 8 ESCs at 50 Hz)

- Approximately 130 bits per extended frame including worst-case stuffing.
- ESC Status: 8 × 50 × 3 frames × 130 bits ≈ **156 kbit/s (15.6 %)**.
- NodeStatus: 9 nodes × 1 Hz × 1 frame ≈ negligible.
- RawCommand at 400 Hz (for reference): approximately 6–8 %.
- **Total is below 25 %**, which is acceptable. The Cycle 1 limit