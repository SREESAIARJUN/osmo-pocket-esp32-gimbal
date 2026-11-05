# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Corrections to the premise

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5047P/D/U and AS5048A/B. The derivation below is identical for any absolute encoder. Only the resolution numbers change, so both are shown.
- **I don't know what "Cycle 23" refers to.** It isn't a SimpleFOC release, API, or term I know. If it is a course module, issue number, or internal label, share its context and I can tailor the answer. Below is the algorithm as implemented in SimpleFOC's `BLDCMotor::initFOC()` → `alignSensor()` (v2.x).

---

## 1. What has to be found

FOC needs the electrical angle θₑ of the rotor d-axis relative to the stator phase-A axis. The sensor gives a mechanical angle θₘ with an arbitrary mounting offset. SimpleFOC models the relationship as:

$$
\theta_e = \mathrm{wrap}_{2\pi}\big(s \cdot p \cdot \theta_m - \theta_{e0}\big)
$$

where:
- p = `pole_pairs`
- s ∈ {+1, −1} = `sensor_direction` (CW/CCW)
- θₑ₀ = `zero_electric_angle`, the unknown offset

For the AS5600, the mechanical angle comes from the raw count:

$$
\theta_m = \frac{2\pi}{4096}\,\text{raw}_{12}
$$

The calibration therefore has two unknowns: **s** and **θₑ₀**. p is assumed known, but it gets sanity-checked.

---

## 2. Step A: direction detection and pole-pair check

The firmware applies an open-loop voltage vector at magnitude `voltage_sensor_align` and slowly sweeps the commanded electrical angle. The rotor magnet follows the stator field.

```cpp
// forward sweep: θe_cmd from 0 → 2π (one electrical rev)
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

// backward sweep: 2π → 0
for (int i = 500; i >= 0; i--) { ...same... }
sensor->update();
float end_angle = sensor->getAngle();
setPhaseVoltage(0, 0, 0);

float moved = fabs(mid_angle - end_angle);
sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;
```

**Direction derivation.** Increasing the commanded θₑ by 2π moves the rotor by +2π/p mechanically, in the motor's positive sense. If the sensor reading increased (end < mid after returning), the sensor and motor agree, so s = +1. Otherwise s = −1.

**Pole-pair check.** One electrical revolution corresponds to Δθₘ = 2π/p. The firmware therefore tests:

$$
\big|\,p\cdot \Delta\theta_m - 2\pi\,\big| > 0.5 \;\Rightarrow\; \text{"PP check: fail"}
$$

A failure means p is wrong, the motor stalled from too little voltage, or the encoder is slipping or misread (common with AS5600 I²C glitches).

---

## 3. Step B: zero-angle finding

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                 // let rotor settle
sensor->update();
zero_electric_angle = 0;     // so electricalAngle() returns raw s·p·θm
zero_electric_angle = electricalAngle();
setPhaseVoltage(0, 0, 0);
```

### Why 3π/2?

`setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

With U_d = 0 and θ = 3π/2 (so sin θ = −1, cos θ = 0):

$$
U_\alpha = U_q,\quad U_\beta = 0
$$

The stator field therefore points purely along the α-axis (phase A), at electrical angle 0. The rotor's permanent-magnet flux aligns with it: the d-axis goes to the minimum-energy position. So at equilibrium, **true θₑ = 0**.

Note that the q-axis voltage at θ = 3π/2 lands on electrical angle 0 because the q-axis leads d by π/2. This is why the code uses U_q with an offset angle instead of U_d at θ = 0. The two are equivalent.

### Solving for the offset

At alignment, θₑ = 0, so:

$$
0 = \mathrm{wrap}(s\,p\,\theta_m^{*} - \theta_{e0})
\;\Rightarrow\;
\boxed{\theta_{e0} = \mathrm{wrap}_{2\pi}\!\left(s\cdot p\cdot \frac{2\pi}{4096}\,\text{raw}^{*}\right)}
$$

Setting `zero_electric_angle = 0` first and then calling `electricalAngle()` computes exactly this expression.

---

## 4. Error budget for the AS5600

Electrical-angle quantization step:

$$
\Delta\theta_e = p\cdot\frac{2\pi}{2^{N}}
$$

| Encoder | Bits | p = 7 | p = 11 | p = 14 |
|---|---|---|---|---|
| AS5600 | 12 | 0.62° | 0.97° | 1.23° |
| AS5047/48 | 14 | 0.15° | 0.24° | 0.31° |

A torque-angle error δ reduces torque by a factor of cos δ, so quantization alone is negligible. The dominant real errors are:

1. **Friction and cogging hold-off.** The rotor stops short of the true alignment by δ ≈ τ_friction / (K_t · I_align). Increase `voltage_sensor_align` within thermal limits to reduce it.
2. **Magnet eccentricity and nonlinearity.** The AS5600 is typically about ±1° mechanical, which is multiplied by p electrically. For example, 1° × 11 = 11° electrical.
3. **AS5600 hysteresis and noise**, plus I²C read errors.

---

## 5. Improved zero-finding (optional, beyond stock SimpleFOC)

Averaging over several electrical positions and both approach directions cancels friction bias and averages out eccentricity:

```cpp
float s_sum = 0, c_sum = 0;
const int K = pole_pairs;            // one sample per electrical rev
for (int k = 0; k < K; k++) {
  for (int dir = -1; dir <= 1; dir += 2) {   // approach from both sides
    float target = _3PI_2 + _2PI * k;        // same electrical angle, different mech pos
    for (int i = 0; i <= 100; i++)           // ramp in from ±0.5 rad
      setPhaseVoltage(Ualign, 0, target + dir * 0.5f * (1 - i / 100.0f)), _delay(2);
    _delay(300);
    sensor->update();
    float e = _normalizeAngle(sensor_direction * pole_pairs * sensor->getMechanicalAngle());
    s_sum += sinf(e); c_sum += cosf(e);      // circular mean (handles 0/2π wrap)
  }
}
zero_electric_angle = _normalizeAngle(atan2f(s_sum, c_sum));
```

The circular mean is essential. A plain arithmetic mean of angles near 0/2π gives garbage.

---

## 6. Skipping calibration at runtime

Once measured, store the results and pass them in so `initFOC()` skips the alignment routine:

```cpp
motor.zero_electric_angle = 2.15f;          // from Serial output of first run
motor.sensor_direction   = Direction::CW;
motor.initFOC();
```

---

## 7. AS5600-specific setup notes

- Use `MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);` and run I²C at 400 kHz or more. The I²C read latency, roughly 100–200 µs, limits loop rate and adds velocity-dependent phase lag.
- Use a diametrically magnetized magnet with an air gap of about 0.5–3 mm, centered within roughly 0.25 mm.
- Check the AGC and MAGNITUDE registers to confirm field strength. A weak or over-strong field degrades accuracy and makes the pole-p