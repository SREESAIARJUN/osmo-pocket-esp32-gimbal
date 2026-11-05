# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections first

- **The AS5600 is 12-bit, not 14-bit.** It gives 4096 counts per revolution, about 0.088° mechanical resolution. The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The derivation below works for any resolution; only the numeric error budget in §5 changes. For the AS5600 use 12 bits.
- **I don't recognize "Cycle 8."** It isn't a term in SimpleFOC or ams documentation. If it refers to your own iteration or course numbering, the material below should still apply. If it refers to something specific, tell me what it is and I'll adapt the answer.

---

## 1. What needs to be calibrated

FOC needs the rotor's **electrical** angle θₑ, meaning the angle of the rotor d-axis (magnet flux) relative to the stator α-axis (phase A). The sensor only gives a **mechanical** angle θₘ, measured from an arbitrary magnet mounting orientation. The two are related by:

$$
\theta_e = \big( s \cdot p \cdot \theta_m \;-\; \theta_0 \big) \bmod 2\pi
$$

| Symbol | Meaning | How it's found |
|---|---|---|
| p | pole pairs | known, or estimated in §3 |
| s ∈ {+1, −1} | sensor direction relative to phase sequence | direction test (§3) |
| θ₀ | electrical zero offset (`zero_electric_angle`) | alignment (§4) |

In SimpleFOC this is `BLDCMotor::electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

Calibration therefore has two jobs: find **s** and find **θ₀**. Both happen in `initFOC()` → `alignSensor()`.

---

## 2. Physics of forced alignment

### 2.1 How SimpleFOC applies voltage

`setPhaseVoltage(Uq, Ud, θ)` performs an inverse Park transform and then SVPWM. With Ud = 0, the stator voltage vector, and in steady state the current and field vector, sits at:

$$
\angle \vec{V}_s = \theta + \tfrac{\pi}{2}
$$

This is because the q-axis leads the d-axis by 90°:

$$
V_\alpha = -U_q \sin\theta, \qquad V_\beta = U_q \cos\theta
$$

### 2.2 Where the rotor settles

A permanent-magnet rotor with no load settles where torque is zero. Torque is proportional to i_q, which is the stator current component perpendicular to the rotor flux. The stable equilibrium is therefore:

$$
\theta_e^{\text{rotor}} = \angle \vec{V}_s = \theta + \tfrac{\pi}{2}
$$

### 2.3 Why SimpleFOC uses 3π/2

Choose the commanded angle so the rotor d-axis lands exactly on the α-axis (phase A), which defines θₑ = 0:

$$
\theta + \tfrac{\pi}{2} \equiv 0 \pmod{2\pi} \;\Rightarrow\; \theta = \tfrac{3\pi}{2}
$$

This is the origin of `_3PI_2` in the code. While held at this angle, the true electrical angle is 0 by definition. Substituting into the model:

$$
0 = s\,p\,\theta_m^{\text{meas}} - \theta_0
\;\Rightarrow\;
\boxed{\theta_0 = (s \cdot p \cdot \theta_m^{\text{meas}}) \bmod 2\pi}
$$

This is the entire offset derivation.

---

## 3. Direction detection and pole-pair check

Command the electrical angle in an open-loop sweep from 0 to 2π (forward), then back (reverse), and record the mechanical angle before and after.

- One electrical revolution produces exactly 1/p of a mechanical revolution.
- The sign of the mechanical change gives the direction:

$$
\Delta\theta_m = \theta_m^{\text{end}} - \theta_m^{\text{mid}}, \qquad
s = \begin{cases} +1 & \Delta\theta_m > 0 \text{ (CW)}\\ -1 & \Delta\theta_m < 0 \text{ (CCW)}\end{cases}
$$

The magnitude gives a pole-pair sanity check:

$$
\hat p = \frac{2\pi}{|\Delta\theta_m|}
$$

If |p̂ − p| > 0.5, SimpleFOC warns "PP check: fail". The cause is usually a wrong `pole_pairs` value, too low `voltage_sensor_align`, or the rotor slipping or cogging.

---

## 4. The complete algorithm (as in SimpleFOC v2.x)

```cpp
int BLDCMotor::alignSensor() {
  // ---------- Step 1: direction & pole-pair check ----------
  if (sensor_direction == Direction::UNKNOWN) {
    // forward sweep: electrical 0 -> 2π
    for (int i = 0; i <= 500; i++) {
      float angle = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, angle);
      sensor->update();
      _delay(2);
    }
    sensor->update();
    float mid_angle = sensor->getAngle();

    // reverse sweep: 2π -> 0  (also cancels backlash/hysteresis bias)
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

    float moved = fabs(mid_angle - end_angle);
    if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;     // rotor didn't move -> fail
    sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;

    // pole pair check
    if (fabs(moved * pole_pairs - _2PI) > 0.5f)
      SIMPLEFOC_DEBUG("MOT: PP check: fail - estimated pp: ", _2PI / moved);
  }

  // ---------- Step 2: electrical zero ----------
  if (!_isset(zero_electric_angle)) {
    setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);    // rotor d-axis -> phase A
    _delay(700);                                          // let it settle
    sensor->update();                                     // AS5600 needs fresh read
    zero_electric_angle = 0;
    zero_electric_angle = electricalAngle();              // = s*p*θm mod 2π
    _delay(20);
    setPhaseVoltage(0, 0, 0);
    _delay(200);
  }
  return 1;
}
```

Setting `zero_electric_angle = 0` before calling `electricalAngle()` is deliberate. It makes `electricalAngle()` return s·p·θₘ mod 2π, which is exactly the boxed formula from §2.3.

### Skipping calibration on subsequent boots

Calibration only needs to run once per mechanical assembly. Afterwards, hard-code the results:

```cpp
motor.initFOC(2.15, Direction::CW);   // zero_electric_angle, sensor_direction (older API)
// newer API:
motor.zero_electric_angle = 2.15;
motor.sensor_direction   = Direction::CW;
motor.initFOC();
```

---

## 5. AS5600-specific concerns and error budget

### 5.1 Quantization

$$
\delta\theta_m = \frac{2\pi}{4096} \approx 1.53\text{ mrad}, \qquad
\delta\theta_e = p\,\delta\theta_m
$$

For a 7-pole-pair gimbal motor: δθₑ ≈ 10.7 mrad ≈ 0.62° electrical.

### 5.2 Torque loss from offset error

An offset error ε puts part of the current on the d-axis instead of the q-axis. Torque scales as cos ε:

$$
\frac{T}{T_{\max}} = \cos\varepsilon
$$

- At 1° electrical error, the loss is about 0.015%, which is negligible.
- At 10° electrical error, the loss is about 1.5%.
- Larger errors also cause asymmetric performance between CW and CCW.

### 5.3 Dominant real-world error sources

These usually matter more than quantization:

1. **Cogging and friction.** The rotor stops short of the true equilibrium.
   - Fix: raise `voltage_sensor_align` (typically 1–3 V for gimbal motors, 0.5–1 V for low-resistance drones).
   - Fix: average the offset at several alignment points (§6).
2. **Magnet eccentricity or misalignment.** This produces a once-per-mechanical-revolution error that gets multiplied by p in the electrical domain. A single-point θ₀ cannot correct it; it requires a lookup table.
3. **I²C latency.** At 400 kHz the AS5600 takes roughly 0.1–0.2 ms per read. This doesn't affect static calibration, but at speed it adds a phase lag of ω·t_read.
   - Fix: use the PWM/analog output or a faster bus.
   - Fix: compensate lag in software.
4. **AS5600 hysteresis and filtering.** Default slow-filter settings add lag and hysteresis.
   - Fix: set the CONF register for the fastest filter (SF = 11, FTH as needed).
   - Fix: always call `sensor->update()` after settling, as the code does.

---

## 6.