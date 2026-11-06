#!/usr/bin/env python3
"""
Revision 7.0 — Encoder I2C bus timing budget (TCA9548A + 3x AS5600)
===================================================================
Bit-level timing of one encoder sweep on I2C0 at the TCA9548A Fast-mode limit,
comparing the Rev 7.0 "fast read" (AS5600 address pointer parked on RAW ANGLE)
with a conventional pointer-write + read, and the resulting commutation-angle
staleness and phase lag at the angle-loop crossover.

Frame model (bits on SCL): START(1) + address byte(9 incl. ACK) + n data bytes(9 each)
+ STOP(1). Repeated-START reads are modelled as a separate frame (conservative).
The firmware host test (firmware/test/host) measures the same quantity from the
bus mock; both must agree (~367 us / sweep with fast read).

Writes results into simulation_report.json under "encoder_bus".
"""
import json
import math

BUS_HZ = 400_000          # TCA9548A max; AS5600 alone would allow 1 MHz FM+
AXES = 3
POLE_PAIRS = 14
ANGLE_LOOP_CROSSOVER_HZ = 6.0     # f_cp target from the Rev 6 tuning synthesis
MAX_SHAFT_RAD_S = 1.0             # aggressive pan slew for a pocket gimbal
COUNTS = 4096                     # AS5600 RAW ANGLE resolution (12-bit)


def frame_bits(data_bytes: int) -> int:
    return 1 + 9 + 9 * data_bytes + 1


def sweep_bits(fast: bool) -> int:
    mux_select = frame_bits(1)                         # write control register
    read = frame_bits(2) if fast else frame_bits(1) + frame_bits(2)
    return AXES * (mux_select + read)


def analyse():
    out = {}
    for mode in ("fast_read", "pointer_write_read"):
        bits = sweep_bits(mode == "fast_read")
        t_sweep = bits / BUS_HZ
        f_axis = 1.0 / t_sweep
        # worst-case staleness seen by loopFOC: one full sweep (sample is published
        # at end of its own read, consumed up to one sweep later)
        stale = t_sweep
        elec_err_deg = math.degrees(MAX_SHAFT_RAD_S * stale * POLE_PAIRS)
        # pure delay phase lag at angle loop crossover
        lag_deg = 360.0 * ANGLE_LOOP_CROSSOVER_HZ * stale
        out[mode] = {
            "bits_per_sweep": bits,
            "sweep_us": round(t_sweep * 1e6, 1),
            "per_axis_update_hz": round(f_axis, 0),
            "worst_case_staleness_us": round(stale * 1e6, 1),
            "commutation_error_deg_elec_at_1rad_s": round(elec_err_deg, 3),
            "phase_lag_at_angle_crossover_deg": round(lag_deg, 3),
        }
    fast = out["fast_read"]
    out["bus_hz"] = BUS_HZ
    out["mux"] = "TCA9548A @0x70, ch0=PAN ch1=ROLL ch2=PITCH, AS5600 @0x36 each"
    out["pins"] = "I2C0 SDA=GPIO17 SCL=GPIO18, MUX RESET_N=GPIO2 (Rev 6 SCL=GPIO22 does not exist on ESP32-S3)"
    out["resolution_deg_mech"] = round(360.0 / COUNTS, 4)
    # Acceptance: per-axis >= 2 kHz (>= 20x the 25 Hz velocity loop *and* well above the
    # 6 Hz angle loop), and commutation error < 15 deg electrical at max slew (>96% torque).
    out["acceptance"] = {
        "per_axis_update_hz_min": 2000,
        "commutation_error_deg_elec_max": 15.0,
        "status": "PASS" if (fast["per_axis_update_hz"] >= 2000 and
                             fast["commutation_error_deg_elec_at_1rad_s"] < 15.0) else "FAIL",
    }
    return out


def main():
    res = analyse()
    try:
        with open("simulation_report.json") as f:
            report = json.load(f)
    except (OSError, ValueError):
        report = {}
    report["encoder_bus"] = res
    with open("simulation_report.json", "w") as f:
        json.dump(report, f, indent=2)
    fr = res["fast_read"]
    print(f"[ENCODER BUS] {fr['sweep_us']} us/sweep -> {fr['per_axis_update_hz']:.0f} Hz/axis, "
          f"{fr['commutation_error_deg_elec_at_1rad_s']} deg elec @1 rad/s, "
          f"{fr['phase_lag_at_angle_crossover_deg']} deg lag @ {ANGLE_LOOP_CROSSOVER_HZ} Hz "
          f"-> {res['acceptance']['status']}")


if __name__ == "__main__":
    main()
