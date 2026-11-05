# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Clarifications

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5047P, AS5048A/B, and AS5147. The derivation below is the same for either. Only the quantization numbers change, and I give both.
- **I don't recognize "Cycle 28."** It isn't a SimpleFOC release name or a term from its documentation that I know of. If it refers to a course module, internal project phase, or forum thread, please share the context. The algorithm below follows SimpleFOC's `BLDCMotor::alignSensor()` as implemented in the 2.x releases.

---

## 1. What needs calibrating

FOC needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator's phase-A axis. The sensor only gives the **mechanical angle** θₘ, measured from an arbitrary magnet mounting position:

$$
\theta_e = \text{normalize}\big(d \cdot p \cdot \theta_m - \theta_0\big)
$$

where:

| Symbol | Meaning |
|---|---|
| p | Pole pairs |
| d ∈ {+1, −1} | Sensor direction relative to the motor's electrical rotation |
| θ₀ | Zero electric angle (the unknown offset) |
| normalize | Wraps the result into [0, 2π) |

Calibration must find **d**, verify **p**, and find **θ₀**.

---

## 2. Why SimpleFOC applies voltage at 3π/2

SimpleFOC commands voltage as `setPhaseVoltage(Uq, Ud, θ)`, which performs an inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

With Ud = 0 and θ = 3π/2:

$$
U_\alpha = -U_q\sin(3\pi/2) = U_q,\qquad U_\beta = U_q\cos(3\pi/2) = 0
$$

So the stator field points along +α, which is the phase-A axis (electrical 0).

A rotor held with no load aligns its d-axis (magnet flux) with the stator field. At equilibrium, therefore:

$$
\theta_e^{\text{true}} = 0
$$

Any sensor reading taken in this state is, by definition, the offset:

$$
\theta_0 = \text{normalize}(d \cdot p \cdot \theta_m^{\text{aligned}})
$$

---

## 3. The algorithm (SimpleFOC `alignSensor`)

### Step A: Direction detection and pole-pair check

The motor is driven open-loop through one electrical revolution forward, then one backward:

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

float moved = fabs(mid_angle - end_angle);
if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;          // failure: no motion
sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;

// pole-pair sanity check: one electrical rev should move 2π/p mechanically
if (fabs(moved * pole_pairs - _2PI) > 0.5f)
  SIMPLEFOC_DEBUG("MOT: PP check: fail - estimated pp: ", _2PI / moved);
```

**Derivation of the checks:**

- One electrical revolution equals 2π/p mechanical radians. So |Δθₘ|·p ≈ 2π, and the estimated pole pairs are p̂ = 2π/|Δθₘ|.
- The sign of Δθₘ during forward electrical motion gives d. That is why `mid_angle < end_angle` (the angle fell on the forward sweep) implies CW, i.e. d = −1.

### Step B: Zero electric angle

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);  // field along phase A
_delay(700);                                        // let the rotor settle
sensor->update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();   // = normalize(d * p * θm - 0)
setPhaseVoltage(0, 0, 0);
_delay(200);
```

Setting θ₀ = 0 before calling `electricalAngle()` makes that call return d·p·θₘ, which is exactly the offset from Section 2.

From then on, every loop computes:

```cpp
float BLDCMotor::electricalAngle() {
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

You can skip calibration on later boots by saving the result and restoring it:

```cpp
motor.zero_electric_angle = 2.1543f;      // value printed by a previous run
motor.sensor_direction = Direction::CW;
motor.initFOC();
```

---

## 4. Error budget for the AS5600

**Quantization.** One count is Δθₘ = 2π/4096 ≈ 0.088° mechanical. In electrical terms that is:

$$
\Delta\theta_e = p \cdot 0.088^\circ
$$

For a 7-pole-pair gimbal motor this is about 0.62° electrical. The resulting torque loss is about 1 − cos(0.62°) ≈ 6×10⁻⁵, which is negligible. A 14-bit sensor reduces this by a factor of 4.

**Rotor equilibrium error.** With cogging torque τ_c and alignment torque τ ≈ K·sin(δ), the rotor settles at:

$$
\delta \approx \arcsin(\tau_c / K)
$$

This is usually the dominant error, often several electrical degrees. Raising `voltage_sensor_align` reduces it, but costs heat. Check the current limit: I = V/R.

**AS5600 specifics:**

- **I²C latency.** Reads take about 100–200 µs at 400 kHz, and in analog mode there is ADC noise. Use I²C at 400 kHz or faster.
- **Magnet quality.** Use a diametric magnet centered with an air gap of roughly 0.5–3 mm. Check the MD/ML/MH status bits.
- **Nonlinearity.** The ±1° typical integral nonlinearity varies around the revolution, so a single-point offset only cancels it at the calibration point.

---

## 5. A more robust variant (recommended improvement)

Average the offset over N electrical positions, approaching each from both directions so friction and cogging errors cancel:

```cpp
float s = 0, c = 0;
for (int k = 0; k < N; k++) {
  for (int dir = -1; dir <= 1; dir += 2) {
    float target = _2PI * k / N;
    // approach from slightly off-target, from each side
    setPhaseVoltage(Ualign, 0, _3PI_2 + target - dir * 0.3f);
    _delay(150);
    setPhaseVoltage(Ualign, 0, _3PI_2 + target);
    _delay(300);
    sensor->update();
    float off = _normalizeAngle(sensor_direction * pole_pairs
                                * sensor->getMechanicalAngle() - target);
    s += sinf(off);
    c += cosf(off);
  }
}
zero_electric_angle = _normalizeAngle(atan2f(s, c));  // circular mean
```

Two details matter here:

- **Circular mean.** Use the circular mean (atan2 of summed sines and cosines), not an arithmetic mean, because offsets near 0 and 2π would otherwise average to π.
- **Choosing N.** Taking N = p·m with m ≥ 2 samples every pole pair. This also averages out magnet eccentricity, which appears at the mechanical frequency.

The stored per-position residuals can serve as a lookup table for nonlinearity compensation. SimpleFOC's `CalibratedSensor` class implements this idea.