# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Corrections to the premise

- **The AS5600 is 12-bit (4096 counts/rev), not 14-bit.** The 14-bit ams parts are the AS5047P/D, AS5048A/B and AS5147. The derivation below is resolution-agnostic. I note where 12 versus 14 bits matters.
- **I don't know what "Cycle 24" refers to.** It is not a SimpleFOC release name or a documented feature. The code below follows SimpleFOC 2.x (`BLDCMotor::alignSensor()` and `initFOC()`). Check your version's `src/BLDCMotor.cpp` if it differs.

---

## 1. What has to be calibrated

FOC needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator α-axis, which is aligned with phase A. The encoder only gives a mechanical angle θₘ with an arbitrary zero and an arbitrary direction:

$$
\theta_e = \mathrm{norm}_{2\pi}\big(p \cdot s \cdot \theta_m - \theta_{e,0}\big)
$$

where:

| Symbol | Meaning | SimpleFOC name |
|---|---|---|
| p | pole pairs | `pole_pairs` |
| s ∈ {+1, −1} | sensor direction relative to the motor's positive electrical rotation | `sensor_direction` |
| θₑ,₀ | electrical offset of the sensor zero | `zero_electric_angle` |

Two unknowns must be found: **s** and **θₑ,₀**. The value of p is either known or verified.

In code this is `BLDCMotor::electricalAngle()`:

```cpp
float electricalAngle() {
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

---

## 2. Why SimpleFOC aligns at θ = 3π/2

SimpleFOC applies voltage through `setPhaseVoltage(Uq, Ud, θ)`. With Ud = 0, the inverse Park transform is:

$$
U_\alpha = -U_q \sin\theta, \qquad U_\beta = U_q \cos\theta
$$

At θ = 3π/2:

$$
U_\alpha = -U_q(-1) = U_q, \qquad U_\beta = 0
$$

So the stator field points exactly along the α-axis (phase A). With no load, the rotor's permanent-magnet d-axis aligns with that field. By definition, this rotor position is **θₑ = 0**.

Whatever the encoder reads at that moment is therefore the offset:

$$
\theta_{e,0} = \mathrm{norm}_{2\pi}\big(p \cdot s \cdot \theta_m^{\text{aligned}}\big)
$$

This is exactly what the code does. It calls `electricalAngle()` while `zero_electric_angle` is still 0.

---

## 3. Direction detection and pole-pair check

The procedure:

1. Spin the field open-loop forward through one electrical revolution, θ: 0 → 2π, in small steps. The rotor follows.
2. Record the mechanical angle at the start and the end, θₘ,mid and θₘ,end.
3. Spin back from 2π → 0, so the rotor returns roughly to where it started.

**Direction.** Let Δθₘ = θₘ,end − θₘ,mid.

- If Δθₘ > 0, the sensor counts the same way as the field, so **s = +1** (`CW`).
- If Δθₘ < 0, then **s = −1** (`CCW`).
- If |Δθₘ| is about 0, the rotor did not move. Report a failure: the voltage is too low, a phase is open, or the magnet is missing.

**Pole-pair check.** One electrical revolution equals 2π/p mechanical. So:

$$
p_{\text{est}} = \frac{2\pi}{|\Delta\theta_m|}
$$

If |p·|Δθₘ| − 2π| > 0.5 rad, SimpleFOC warns "PP check: fail". This catches a wrong `pole_pairs` value or a slipping rotor.

---

## 4. Full algorithm (as in SimpleFOC)

```cpp
int BLDCMotor::alignSensor() {
  // ---- 1. Direction + pole-pair check (skip if direction already known) ----
  if (sensor_direction == Direction::UNKNOWN) {
    for (int i = 0; i <= 500; i++) {                       // forward 0 -> 2π electrical
      float angle = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, angle);
      sensor->update();
      _delay(2);
    }
    sensor->update();
    float mid_angle = sensor->getAngle();

    for (int i = 500; i >= 0; i--) {                       // backward 2π -> 0
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
    if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;       // rotor did not move -> fail
    sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;

    if (fabs(moved * pole_pairs - _2PI) > 0.5f)            // pole-pair sanity check
      SIMPLEFOC_DEBUG("MOT: PP check: fail - estimated pp: ", _2PI / moved);
  }

  // ---- 2. Zero electrical angle ----
  if (!_isset(zero_electric_angle)) {
    setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);      // field along alpha-axis
    _delay(700);                                           // let the rotor settle
    sensor->update();                                      // AS5600 needs update() before read
    zero_electric_angle = 0;
    zero_electric_angle = electricalAngle();               // = norm(p*s*θm)
    _delay(20);
    setPhaseVoltage(0, 0, 0);
    _delay(200);
  }
  return 1;
}
```

The sign convention in the direction step follows the library. The backward sweep returns the rotor toward its start, so `mid` and `end` straddle one electrical revolution. Only the sign of the difference matters.

---

## 5. AS5600-specific considerations

**Wiring the sensor in.**
- Use `MagneticSensorI2C sensor(AS5600_I2C)`. It reads RAW ANGLE (registers 0x0C–0x0D, 12 bits).
- Ignore the chip's ZPOS/MPOS registers. SimpleFOC handles the offset in software, and programming ZPOS (burn) is unnecessary.

**Quantization error in the electrical angle.**

$$
\delta\theta_e = p \cdot \frac{360^\circ}{4096}
$$

For a 7-pole-pair gimbal motor this gives about 0.62° electrical, which is negligible: cos(0.62°) ≈ 0.99994. A 14-bit sensor would give 0.15°. The resolution difference matters for velocity noise, not for offset calibration.

**Bus speed.**
- At 100 kHz I2C, one read takes about 0.5 ms.
- Use `Wire.setClock(400000)` or faster. Otherwise the sweep's `sensor->update()` calls dominate timing and the closed-loop rate suffers.

**Magnet mounting.**
- Check the MAGNET STATUS register (0x0B) for the MD/ML/MH bits.
- A weak or off-axis magnet produces nonlinearity, typically a once-per-revolution sinusoidal error of about 1° mechanical.
- Multiplied by p, this becomes the dominant offset error (7° electrical at p = 7). A single-point calibration can't remove it.

---

## 6. Improved offset estimation (optional)

Single-point alignment suffers from three error sources:
- cogging and friction, which stop the rotor short of the field,
- the encoder nonlinearity described above,
- quantization.

A more robust version samples many electrical positions in both directions and takes a **circular mean** of the residuals.

For each commanded electrical angle θᵏ = 2πk/N, with k = 0…N−1 over all p electrical cycles, the residual is:

$$
e_k = \mathrm{norm}\big(p\,s\,\theta_{m,k} - (\theta^k - \tfrac{3\pi}{2})\big)
$$

Here θᵏ − 3π/2 is the field direction relative to α, i.e. the true electrical position under the Park convention above. The offset is the circular mean:

$$
\theta_{e,0} = \operatorname{atan2}\Big(\sum_k \sin e_k,\ \sum_k \cos e_k\Big)
$$

Average the forward and backward sweeps so that friction lag, which has opposite sign in each direction, cancels:

```cpp
float s_sum = 0, c_sum = 0;
const int N = 64 * pole_pairs;
for (int dir = 0; dir < 2; dir++) {
  for (int i = 0; i < N; i++) {
    int k = dir ? (N - 1 - i) : i;
    float cmd = _2PI *