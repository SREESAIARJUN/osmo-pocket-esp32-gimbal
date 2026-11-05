# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Two corrections first

- **The AS5600 is a 12-bit sensor** (4096 counts/rev), not 14-bit. The 14-bit parts are the AS5047P/AS5048A/B. The math below works for any resolution N. I'll use N = 12 and note where it matters.
- **"Cycle 31"** isn't a SimpleFOC term I recognize. It isn't a library version or a documented calibration phase. If it refers to your own project iteration or a course module, share the context and I'll adapt this. What follows is the algorithm SimpleFOC actually implements in `FOCMotor::alignSensor()` / `BLDCMotor::alignSensor()`, derived from first principles, plus refinements.

---

## 1. The model

SimpleFOC computes the electrical angle as:

$$
\theta_e = \text{normalize}\big(d \cdot p \cdot \theta_m - \theta_0\big)
$$

where:
- θ_m is the shaft angle from the sensor,
- p is the number of pole pairs,
- d ∈ {+1, −1} is the sensor direction (CW or CCW relative to the phase sequence),
- θ₀ is `zero_electric_angle`, the unknown offset.

θ₀ exists because the magnet is glued onto the shaft at an arbitrary orientation relative to the rotor's magnetic d-axis.

The calibration must find **d** (and sanity-check **p**), then **θ₀**.

---

## 2. Why the alignment angle is 3π/2

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta, \qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

Calibration uses U_d = 0 and U_q = V_align. Setting θ = 3π/2 gives:

$$
U_\alpha = -V\sin(3\pi/2) = +V, \qquad U_\beta = V\cos(3\pi/2) = 0
$$

The resulting stator field points along the **α-axis (phase A)**.

The rotor's permanent-magnet d-axis snaps into alignment with that field. By definition, this rotor position is **electrical angle 0**. Whatever the sensor reads there is the offset:

$$
\theta_e^{true} = 0 = d\,p\,\theta_m^{meas} - \theta_0
\;\;\Rightarrow\;\;
\theta_0 = \text{normalize}(d\,p\,\theta_m^{meas})
$$

That is why the code sets `zero_electric_angle = 0` and then calls `electricalAngle()`.

---

## 3. Step A: direction and pole-pair check

The procedure drives an open-loop electrical rotation forward, then backward, and compares the sensor readings.

```cpp
// Forward one electrical revolution
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

// Backward one electrical revolution
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
if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;          // rotor didn't move: fail
sensor_direction = (mid_angle < end_angle) ? Direction::CCW
                                           : Direction::CW;

// Pole-pair sanity check: one electrical rev = 2π/p mechanical
if (fabs(moved * pole_pairs - _2PI) > 0.5f)
  SIMPLEFOC_DEBUG("MOT: PP check: fail - estimated pp: ", _2PI / moved);
```

**How the decisions follow from the model:**

- **Direction.** Going forward one electrical revolution moves the shaft by +2π/p in the phase-sequence direction. If the sensor angle increased (mid > end), the sensor and phase sequence agree, so d = +1 (CW in SimpleFOC's naming). Otherwise d = −1.
- **Pole pairs.** The estimate is p̂ = 2π / |Δθ_m|. The 0.5 rad tolerance absorbs cogging and friction lag.
- **Why two sweeps.** The backward pass returns the rotor to the start. It also means `moved` measures a full ±1 electrical revolution with roughly symmetric lag.

---

## 4. Step B: zero-angle measurement

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);   // field on α-axis
_delay(700);                                         // let rotor settle
sensor->update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();             // = norm(d·p·θm)
setPhaseVoltage(0, 0, 0);
_delay(200);
```

You can skip calibration on later boots by storing the results:

```cpp
motor.zero_electric_angle = 2.15f;           // value printed from a previous run
motor.sensor_direction    = Direction::CW;
```

---

## 5. AS5600-specific setup

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
// equivalent to: MagneticSensorI2C(0x36, 12, 0x0C, 4);
//   address 0x36, 12 bits, RAW ANGLE register 0x0C (MSB), 4 bits used in MSB byte

void setup() {
  sensor.init();
  Wire.setClock(400000);              // or 1 MHz if the MCU supports it
  motor.linkSensor(&sensor);
  motor.voltage_sensor_align = 3;     // high enough to overcome cogging/friction
  motor.init();
  motor.initFOC();                    // runs Steps A and B
}
```

**Quantization error.** One AS5600 count is 2π/4096 ≈ 1.53 mrad mechanical. In electrical terms that is p · 1.53 mrad:

- 7 pole pairs: ≈ 10.7 mrad ≈ 0.61°
- 14 pole pairs: ≈ 1.2°

These errors are negligible. Torque loss scales as 1 − cos(Δθ), which is below 0.01%.

**Practical error sources that dominate:**

1. **Use the RAW ANGLE register (0x0C).** The ANGLE register (0x0E) is post-processed by the chip's hysteresis and ZPOS/MPOS settings. Those would corrupt θ₀ if anyone later reprograms them.
2. **Friction and cogging lag.** With a load torque τ_f, the rotor stops short of the field by roughly δ ≈ arcsin(τ_f / (k_t · i_align)) electrical. Increase `voltage_sensor_align` (watching the current), or use the bidirectional averaging below.
3. **Magnet eccentricity and air gap.** These produce a once-per-mechanical-revolution sinusoidal error, typically 0.5–2° mechanical, which is multiplied by p. A single-point θ₀ cannot correct it.
4. **I²C latency.** The read takes about 100–200 µs at 400 kHz. This does not affect static calibration, but it does cause phase lag at speed.

---

## 6. Improved zero-finding algorithm (recommended)

### 6a. Bidirectional, multi-pole averaging

Friction lag has opposite sign for forward and backward approach, so averaging the two cancels it to first order. Sampling each electrical zero around the mechanical revolution also averages out eccentricity.

For every electrical-zero position k = 0 … p−1:

- Approach θ = 3π/2 from the forward direction, settle, and read offset_k⁺.
- Approach from the backward direction, settle, and read offset_k⁻.

Then take the **circular mean** of all 2p samples:

$$
\theta_0 = \operatorname{atan2}\Big(\sum_k \sin o_k,\; \sum_k \cos o_k\Big)
$$

The circular mean is essential. An arithmetic mean breaks when samples straddle the 0/2π wrap.

```cpp
float calibrateZeroRobust(BLDCMotor& m, Sensor& s, int pp, float V) {
  float sx = 0, cx = 0;
  for (int k = 0; k < pp; k++) {
    for (int dir = -1; dir <= 1; dir += 2) {
      // approach the k-th electrical zero from direction `dir`
      