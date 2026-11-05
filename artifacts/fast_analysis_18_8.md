# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

**Two corrections first:**

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5047P and AS5048A/B. The math below works for any resolution N. I use N = 12 where numbers matter.
- **I don't know what "Cycle 18" refers to.** It isn't a SimpleFOC release name or a step in its algorithm. If it is your own iteration label or a course module, tell me the context and I can tailor this. What follows is the algorithm as implemented in `BLDCMotor::initFOC()` → `alignSensor()` (v2.x).

---

## 1. What we're solving for

FOC needs the rotor's **electrical** angle θₑ, meaning the angle of the rotor d-axis relative to the stator α-axis (phase A). The sensor gives a **mechanical** angle θₘ with an arbitrary mounting offset. For a motor with *p* pole pairs:

$$
\theta_e = \operatorname{norm}_{2\pi}\big(s \cdot p \cdot \theta_m - \theta_{e,0}\big)
$$

The terms are:
- *s* ∈ {+1, −1}: the sensor direction, i.e. whether the sensor counts the same way as the electrical field rotates.
- *p*: the pole pairs, assumed known; calibration only sanity-checks it.
- θₑ,₀: the **zero electrical angle**, the unknown offset.

In SimpleFOC code this is exactly:

```cpp
float BLDCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

Calibration therefore has two unknowns: **s** and **θₑ,₀**.

---

## 2. The physical principle: open-loop locking

`setPhaseVoltage(Uq, Ud, θ)` performs an inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

Applying a voltage with no closed loop creates a stator field at a commanded angle. The rotor's d-axis (its magnet flux) aligns with that field, because that is the minimum-energy position. The rotor's electrical angle then equals the field angle, which we know.

---

## 3. Step A: direction detection (find *s*)

The procedure sweeps the field one full electrical revolution forward, then back:

```cpp
// forward sweep
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

// backward sweep
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

**Derivation.**
- One electrical revolution (2π electrical) moves the rotor 2π/p mechanically.
- The forward-then-back sweep makes the rotor travel +2π/p, then −2π/p.
- Comparing the angle after the forward sweep (`mid`) to the angle after the return (`end`) gives the sign of how the sensor counts:

$$
s = \begin{cases} +1\ (\text{CW}) & \text{if } \theta_{mid} > \theta_{end} \\ -1\ (\text{CCW}) & \text{if } \theta_{mid} < \theta_{end}\end{cases}
$$

```cpp
float moved = fabs(mid_angle - end_angle);
if (moved < MIN_ANGLE_DETECT_MOVEMENT) { /* fail: rotor didn't move */ }
sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;
```

`getAngle()` (multi-turn, unwrapped) is used rather than the 0–2π mechanical angle so that wrap-around doesn't corrupt the comparison.

**Pole-pair check (free byproduct).** The measured travel should satisfy |Δθₘ| · p ≈ 2π:

```cpp
if (fabs(moved * pole_pairs - _2PI) > 0.5f) { /* warn: PP check failed, estimated pp = 2π/moved */ }
```

---

## 4. Step B: zero electrical angle (find θₑ,₀)

**Choose the lock angle.** We want the field along the α-axis, i.e. electrical angle 0. SimpleFOC applies the voltage as **Uq** (with Ud = 0) at θ = 3π/2:

$$
U_\alpha = -U_q\sin\tfrac{3\pi}{2} = +U_q,\qquad U_\beta = U_q\cos\tfrac{3\pi}{2} = 0
$$

The field points purely along +α, so the rotor locks at **θₑ = 0**. This is also why the direction sweep starts at 3π/2: it starts from the same physical position.

**Solve for the offset.** At lock θₑ = 0, so

$$
0 = s\,p\,\theta_m^{lock} - \theta_{e,0}\ \Rightarrow\ \theta_{e,0} = \operatorname{norm}_{2\pi}(s\,p\,\theta_m^{lock})
$$

SimpleFOC computes this neatly by zeroing the offset and calling `electricalAngle()` itself:

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                    // let rotor settle (mechanical time constant + ringing)
sensor->update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();   // = norm(s·p·θm_lock)
setPhaseVoltage(0, 0, 0);
_delay(200);
```

Once you have the result, you can skip calibration on future boots:

```cpp
motor.zero_electric_angle = 2.15f;           // value printed during first calibration
motor.sensor_direction    = Direction::CW;
motor.initFOC();                             // skips alignment
```

---

## 5. AS5600-specific error budget

| Quantity | Value |
|---|---|
| Mechanical resolution | 2π / 4096 ≈ 1.53 mrad (0.088°) |
| Electrical resolution | p × 0.088° (e.g. p = 7 → 0.62° e) |
| Quantization error in θₑ,₀ | ±p·π/4096 (≈ ±0.31° e at p = 7) |
| I²C read (400 kHz, 2 bytes from 0x0C) | ≈ 100–150 µs → limits loop rate |

Typical sensor setup:

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);  // addr 0x36, reg 0x0C, 12 bit
```

A torque-producing error of δ in θₑ,₀ scales q-axis torque by cos δ and leaks sin δ into the d-axis. Quantization is therefore negligible.

The dominant errors are mechanical:
- **Friction and cogging.** The rotor stops short of the true lock by δ_f ≈ τ_friction / (k · I_align).
- **Magnet misalignment and eccentricity.** These cause a periodic nonlinearity in the AS5600 reading, often ±1° mechanical. That becomes ±p° electrical, which is larger than quantization.

---

## 6. Improved zero-finding (more robust than the default)

Single-point locking is biased by friction and by sensor nonlinearity. A better estimator locks at K electrical angles across several electrical revolutions, approached from both directions, and takes a **circular mean** of the residuals:

$$
\theta_{e,0} = \operatorname{atan2}\!\Big(\sum_k \sin r_k,\ \sum_k \cos r_k\Big),\quad
r_k = s\,p\,\theta_{m,k} - \phi_k
$$

Here φₖ is the commanded electrical angle. φₖ = 0 corresponds to `setPhaseVoltage(U, 0, 3π/2 + φₖ)`.

```cpp
float calibrateZero(