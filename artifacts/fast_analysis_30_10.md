# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections first

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5047P/D, AS5048A/B and AS5147. The math below works for any resolution, but the error budget uses 12 bits.
- **I don't know what "Cycle 30" refers to.** It isn't a SimpleFOC release or a term in its documentation. If it's your project iteration or a specific library version, tell me and I'll adjust. The derivation below follows `BLDCMotor::initFOC()` / `alignSensor()` as implemented in SimpleFOC 2.x.

---

## 1. The model

FOC needs the rotor's **electrical** angle θₑ. The sensor only gives the **mechanical** angle θₘ:

$$
\theta_m = \frac{2\pi}{4096}\,\text{RAW\_ANGLE}[11:0]
$$

The two are related by three unknowns:

$$
\boxed{\theta_e = \operatorname{wrap}_{2\pi}\big(d \cdot p \cdot \theta_m - \theta_0\big)}
$$

| Symbol | Meaning | How it's found |
|---|---|---|
| p | pole pairs | user-supplied, sanity-checked |
| d ∈ {+1, −1} | sensor direction (CW/CCW) relative to phase sequence | direction test |
| θ₀ | `zero_electric_angle`, the offset between magnet zero and the phase-A d-axis | alignment |

In SimpleFOC this is exactly:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

Calibration means determining d and θ₀ (and validating p).

---

## 2. Why SimpleFOC drives at 3π/2

`setPhaseVoltage(Uq, Ud, angle_el)` applies the voltage vector in the rotating frame. The inverse Park transform gives a stator-frame vector angle of:

$$
\angle \vec V = \angle_{el} + \operatorname{atan2}(U_q, U_d)
$$

With U_d = 0 and U_q = V_align > 0:

$$
\angle \vec V = \angle_{el} + \frac{\pi}{2}
$$

Choosing ∠_el = 3π/2 gives ∠V = 2π ≡ 0. The stator field then points along phase A.

The rotor's permanent-magnet d-axis aligns with an applied field. So after settling, the rotor sits where the **true** electrical angle is 0. Whatever the sensor reports at that position, scaled by d·p, *is* the offset:

$$
\theta_e^{true} = 0 = d\,p\,\theta_m^{*} - \theta_0 \;\;\Rightarrow\;\; \boxed{\theta_0 = \operatorname{wrap}(d\,p\,\theta_m^{*})}
$$

---

## 3. Step A: direction detection and pole-pair check

The motor is driven open-loop through exactly **one electrical revolution forward, then one back**.

```cpp
// forward
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();      // multi-turn angle

// backward
for (int i = 500; i >= 0; i--) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float end_angle = sensor->getAngle();
setPhaseVoltage(0, 0, 0);
_delay(200);
```

**Direction.** On the backward sweep the rotor returns, so the sign of (mid − end) shows which way the sensor counted while the field rotated positively:

$$
d = \begin{cases} +1\ (\text{CW}) & \text{mid} > \text{end}\\ -1\ (\text{CCW}) & \text{mid} < \text{end} \\ \text{fail} & \text{mid} = \text{end}\ (\text{no motion}) \end{cases}
$$

Equal readings mean the sensor isn't reading, the voltage is too low, or the rotor is stuck.

**Pole-pair check.** One electrical revolution equals 2π/p mechanical radians. With Δ = |mid − end|:

$$
|\,\Delta \cdot p - 2\pi\,| < 0.5 \ \text{rad} \quad\Rightarrow\quad \text{PP check OK}
$$

You can also estimate p directly as p̂ = round(2π/Δ). A failure usually means a wrong `pole_pairs` value, slip under load or cogging, or `voltage_sensor_align` being too low.

Use `getAngle()` (multi-turn, with full-rotation tracking) here, **not** `getMechanicalAngle()`. Otherwise a sweep that crosses the AS5600's 4095→0 wrap corrupts Δ.

---

## 4. Step B: zero offset

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);  // field along phase A
_delay(700);                                       // let rotor settle
sensor->update();
zero_electric_angle = 0;                            // so electricalAngle() returns raw d*p*θm
zero_electric_angle = electricalAngle();            // θ0 = wrap(d·p·θm*)
_delay(20);
setPhaseVoltage(0, 0, 0);
_delay(200);
```

Setting `zero_electric_angle = 0` first makes `electricalAngle()` return wrap(d·p·θₘ*), which is exactly the result from §2.

**Skipping calibration on later boots.** Print θ₀ and d once, then hard-code them:

```cpp
motor.zero_electric_angle = 2.4512;            // from a previous calibration
motor.sensor_direction   = Direction::CW;
motor.initFOC();                               // skips alignment
```

This only stays valid if the magnet doesn't move relative to the shaft.

---

## 5. AS5600-specific error budget

| Quantity | Value |
|---|---|
| Mechanical LSB | 2π/4096 = 1.534 mrad = 0.0879° |
| Electrical LSB | p · 0.0879° (7 pp → 0.615°, 11 pp → 0.967°) |
| θ₀ quantization error | ± p·π/4096 (7 pp → ±0.31° el.) |
| Torque loss from offset error ε | 1 − cos ε ≈ ε²/2 → negligible (<0.01 %) |

Quantization is **not** the real limit. The dominant error sources are:

1. **Magnet eccentricity / INL.** Typically ±0.5–1° mechanical, which multiplies by p. At 11 pp this is ~±10° electrical, varying with position.
2. **Settling error.** Friction and cogging stop the rotor short of the true d-axis by a few electrical degrees.
3. **I²C latency.** At 400 kHz a 2-byte read takes ~100–150 µs. At speed ω this lags the angle by ω·t (electrical lag is p·ω·t). Use 400 kHz or 1 MHz (`Wire.setClock(1000000)`, which the AS5600 supports). Alternatively, use the PWM/analog output, or compensate the lag in software.
4. **Hysteresis/filtering.** Leave the AS5600 CONF register's slow filter (SF) at its fastest setting (11) with FTH off for FOC. Heavy filtering adds phase lag.

---

## 6. An improved, more robust zero-finding algorithm

Single-point alignment is biased by friction (2) and position-dependent INL (1). A better estimator samples N electrical angles in both directions and takes the circular mean of the residuals.

At each commanded field angle φₖ (the true θₑ after settling), the residual is:

$$
r_k = d\,p\,\theta_{m,k} - \varphi_k
$$

The estimated offset is their circular mean:

$