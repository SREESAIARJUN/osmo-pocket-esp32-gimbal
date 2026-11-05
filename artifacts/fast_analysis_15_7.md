# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Corrections to the question

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts commonly used with SimpleFOC are the AS5047P and AS5048A/B (16384 counts/rev). The algorithm below is identical for both; only the resolution numbers change. I give both where it matters.
- **I don't know what "Cycle 15" refers to.** It isn't a SimpleFOC version, release, or documented stage. If it comes from a course, internal roadmap or specific document, tell me its context and I can tailor the answer. What follows is the actual `alignSensor()` procedure in SimpleFOC 2.x.

---

## 1. What we need to find

FOC needs the rotor's **electrical** angle θₑ, but the sensor reports a **mechanical** angle θₘ with an arbitrary mounting offset. The relationship is:

$$
\theta_e = \text{normalize}\big(\, d \cdot p \cdot \theta_m \;-\; \theta_0 \,\big)
$$

| Symbol | Meaning |
|---|---|
| p | pole pairs |
| d ∈ {+1, −1} | sensor direction relative to the motor's electrical rotation |
| θ₀ | `zero_electric_angle`, the unknown offset |

This is exactly SimpleFOC's `electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

Calibration must determine **d** first, then **θ₀**. Ideally it also verifies **p**.

---

## 2. Sensor layer: AS5600 raw angle to radians

`MagneticSensorI2C` with the `AS5600_I2C` config uses:
- I²C address `0x36`
- register `0x0C` (RAW ANGLE)
- 12 data bits

The conversion to radians is:

$$
\theta_m^{raw} = \frac{\text{raw}}{4096}\cdot 2\pi \quad (\text{AS5047/48: } /16384)
$$

`Sensor::update()` also tracks full rotations. If the change between samples exceeds 0.8·2π, it assumes a wrap and increments or decrements `full_rotations`. So:
- `getAngle()` is multi-turn and continuous.
- `getMechanicalAngle()` is in [0, 2π).

This matters for the direction test below, which uses `getAngle()` to avoid wrap discontinuities.

---

## 3. Key derivation: why the rotor locks at θₑ = 0 when we command 3π/2

`setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform. With Ud = 0:

$$
U_\alpha = -U_q \sin\theta, \qquad U_\beta = U_q \cos\theta
$$

At θ = 3π/2:

$$
U_\alpha = -U_q(-1) = U_q, \qquad U_\beta = U_q \cdot 0 = 0
$$

So the stator field points exactly along the **α-axis (phase A)**. With no load, the rotor's d-axis (its flux axis) aligns with the applied field. That rotor position, by definition, is **θₑ = 0**.

The q-axis voltage vector always leads the angle argument by π/2. Commanding 3π/2 therefore puts the vector at 2π ≡ 0. This is why the magic constant `_3PI_2` appears in the code.

---

## 4. Step A: direction detection (and pole-pair sanity check)

Sweep the electrical field one full electrical revolution forward, then back, in open loop:

```cpp
// forward: one electrical revolution
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

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

**Direction logic.** The forward sweep increased θₑ by 2π.
- If the sensor reading rose (mid > end after returning), the sensor agrees with the electrical direction, so d = +1 (`CW`).
- Otherwise d = −1 (`CCW`).
- If mid == end, the rotor didn't move. This means a wiring, voltage, or sensor fault, and alignment fails.

```cpp
float moved = fabs(mid_angle - end_angle);
if (mid_angle == end_angle)      { /* fail: no movement */ return 0; }
else if (mid_angle < end_angle)  sensor_direction = Direction::CCW;
else                             sensor_direction = Direction::CW;
```

**Pole-pair check.** One electrical revolution equals 2π/p mechanical. So we expect p · moved ≈ 2π:

```cpp
if (fabs(moved * pole_pairs - _2PI) > 0.5f) {
  // PP check failed: estimated pp = _2PI / moved
}
```

This only warns; it doesn't abort. A failure usually means a wrong `pole_pairs` setting, slip due to low `voltage_sensor_align`, or cogging/friction.

---

## 5. Step B: zero-offset measurement

Lock the rotor at θₑ = 0 (per section 3), let it settle, and read the sensor:

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);   // field on alpha axis
_delay(700);                                        // settle oscillations
sensor->update();
zero_electric_angle = 0;                            // so electricalAngle() returns raw d*p*θm
zero_electric_angle = electricalAngle();            // θ0 = normalize(d·p·θm_locked)
setPhaseVoltage(0, 0, 0);
```

**Proof.** At lock, the true θₑ = 0. Substituting into section 1:

$$
0 = d\,p\,\theta_m^{lock} - \theta_0 \;\Rightarrow\; \theta_0 = \text{normalize}(d\,p\,\theta_m^{lock})
$$

Setting `zero_electric_angle = 0` before calling `electricalAngle()` makes that function return exactly this expression.

---

## 6. Accuracy budget

| Error source | Effect on θₑ |
|---|---|
| Quantization, AS5600 (12-bit) | ±½·(360°/4096)·p = ±0.044°·p (e.g. p=7 gives ±0.31°) |
| Quantization, AS5047/48 (14-bit) | ±0.011°·p |
| AS5600 INL (±~1° typ. with magnet misalignment) | up to ±1°·p, which usually dominates |
| Load/friction torque during lock | rotor lags the field by an angle δ where τ_load = K·sin δ |
| Cogging | rotor settles into a detent near, not at, θₑ = 0 |

Torque is proportional to cos(Δθₑ), so a 5° electrical error costs only about 0.4% of torque. However, it also couples into the d-axis, causing extra current and heating. Larger errors (more than 30°) cause visible asymmetry between directions and inefficiency.

**Practical tips:**
- Run calibration unloaded.
- Raise `voltage_sensor_align` until the pole-pair check passes, but keep it low enough to avoid overheating.
- Center the magnet within about 0.25 mm.
- With the AS5600, use I²C at 400 kHz or faster, or the analog/PWM output with `MagneticSensorAnalog` / `MagneticSensorPWM`.

---

## 7. Skipping calibration on later boots

Print the results once:

```cpp
motor.monitor_variables  // or Serial.println(motor.zero_electric_