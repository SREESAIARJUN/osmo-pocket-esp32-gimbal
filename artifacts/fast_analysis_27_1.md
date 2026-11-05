# DroneCAN Heartbeat and ESC Telemetry Message Definitions for ArduPilot on STM32H743

> **Note on "Cycle 27":** I don't recognize this as a standard DroneCAN or ArduPilot term. It may be your project's internal iteration label. If it refers to a specific requirement, such as a 27 ms publish cycle or a particular release, tell me and I'll adapt the rates or code.

---

## 1. Heartbeat: `uavcan.protocol.NodeStatus`

**Data Type ID:** 341 (broadcast) | **Payload:** 56 bits (7 bytes, single frame)

```text
# uavcan/protocol/341.NodeStatus.uavcan
# Mandatory heartbeat. Every node must publish this.

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

**Recommended rate:** 1 Hz. Peers mark the node offline after 3 s without a heartbeat.

---

## 2. ESC Telemetry: `uavcan.equipment.esc.Status`

**Data Type ID:** 1034 (broadcast) | **Payload:** 110 bits (14 bytes)

On classic CAN this payload needs a multi-frame transfer with a transfer CRC. On CAN-FD it fits in a single frame.

```text
# uavcan/equipment/esc/1034.Status.uavcan
# Generic ESC status. Publish once per ESC.

uint32  error_count        # Cumulative resettable error counter
float16 voltage            # Volts
float16 current            # Amperes. Negative = regen/braking
float16 temperature        # KELVIN (convert from °C: +273.15)
int18   rpm                # Signed. Negative = reverse rotation
uint7   power_rating_pct   # 0..127, % of max power (instantaneous)
uint5   esc_index          # 0..31, matches RawCommand/RPMCommand index
```

### Optional: `uavcan.equipment.esc.StatusExtended` (ID 1036)

```text
uint7  input_pct            # Throttle demand, %
uint7  output_pct           # Actual output, %
int9   motor_temperature_degC
uint10 motor_angle          # Electrical angle, if available
uint19 status               # Vendor-specific status/fault bits
uint5  esc_index
```

---

## 3. Publishing Code (AP_Periph, STM32H743)

On an H743 node the ESC-side firmware is usually **AP_Periph**. The following sketch follows AP_Periph's `canard_broadcast` pattern.

```cpp
// ---- Heartbeat (called from AP_Periph_FW::can_update at 1 Hz) ----
void AP_Periph_FW::send_node_status()
{
    uavcan_protocol_NodeStatus ns {};
    ns.uptime_sec  = AP_HAL::millis() / 1000U;
    ns.health      = node_health;                  // UAVCAN_PROTOCOL_NODESTATUS_HEALTH_*
    ns.mode        = node_mode;                    // UAVCAN_PROTOCOL_NODESTATUS_MODE_*
    ns.sub_mode    = 0;
    ns.vendor_specific_status_code = vendor_status; // e.g. hal.util->available_memory() clipped

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
    const uint32_t len = uavcan_protocol_NodeStatus_encode(&ns, buffer, !canfdout());

    canard_broadcast(UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                     UAVCAN_PROTOCOL_NODESTATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}

// ---- ESC telemetry (one transfer per ESC) ----
void AP_Periph_FW::send_esc_status(uint8_t esc_index,
                                   const AP_ESC_Telem_Backend::TelemetryData &td,
                                   float rpm)
{
    uavcan_equipment_esc_Status pkt {};
    pkt.esc_index        = esc_index & 0x1F;
    pkt.voltage          = td.voltage;
    pkt.current          = td.current;
    pkt.temperature      = C_TO_KELVIN(td.temperature_cdeg * 0.01f); // Kelvin
    pkt.rpm              = constrain_int32(lrintf(rpm), -131072, 131071); // int18 range
    pkt.power_rating_pct = 0;           // fill if ESC reports it
    pkt.error_count      = td.count;    // or ESC-reported error counter

    uint8_t buffer[UAVCAN_EQUIPMENT_ESC_STATUS_MAX_SIZE];
    const uint32_t len = uavcan_equipment_esc_Status_encode(&pkt, buffer, !canfdout());

    canard_broadcast(UAVCAN_EQUIPMENT_ESC_STATUS_SIGNATURE,
                     UAVCAN_EQUIPMENT_ESC_STATUS_ID,
                     CANARD_TRANSFER_PRIORITY_LOW,
                     buffer, len);
}
```

The autopilot side (`AP_DroneCAN`) already subscribes to `esc.Status` and feeds `AP_ESC_Telem`. No flight-controller code changes are needed.

---

## 4. STM32H743-Specific Notes

| Item | Detail |
|---|---|
| CAN peripheral | FDCAN1/FDCAN2 (H7 uses FDCAN, not bxCAN). Supports CAN-FD if enabled. |
| hwdef | `PD0 CAN1_RX CAN1`, `PD1 CAN1_TX CAN1` (pins board-dependent); `define HAL_NUM_CAN_IFACES 1` |
| Message RAM | FDCAN shared message RAM is 10 KB on H7. ChibiOS HAL partitions it; avoid oversized RX FIFOs if using 2 interfaces. |
| Autopilot params | `CAN_P1_DRIVER=1`, `CAN_D1_PROTOCOL=1` (DroneCAN), `CAN_D1_UC_ESC_BM` = ESC bitmask |
| Periph params | `CAN_NODE` (or DNA), `ESC_TELEM_PORT`, `ESC_TELEM_RATE` (Hz) |

---

## 5. Bus Budget (classic CAN, 1 Mbit/s)

| Message | Frames | Rate | Approx. load |
|---|---|---|---|
| NodeStatus | 1 | 1 Hz | negligible |
| esc.Status × 4 ESCs | 3 each (14 B + 2 B CRC over 7-byte chunks) | 50 Hz | ~600 frames/s ≈ 7–8 % |
| esc.Status