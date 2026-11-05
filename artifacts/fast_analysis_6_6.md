# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections first

- **The AS5600 is 12-bit, not 14-bit.** It outputs 4096 counts per revolution (≈0.088°). The 14-bit parts are the AS5047P/AS5048A (SPI, 16384 counts). The algorithm below is identical for both. Only the CPR and the bus change.
- **"Cycle 6" is not a SimpleFOC term I recognize.** If it refers to a specific course, lab, or release step, tell me what it covers. The derivation below follows SimpleFOC's `initFOC()` → `alignSensor()` as implemented in the 2.x library.

---

## 1. What problem are we solving?

FOC needs the **electrical angle** θₑ of the rotor's d-axis relative to stator phase A. The encoder gives a **mechanical angle** θₘ measured from an arbitrary magnet orientation. The two are related by:

$$
\theta_e = \text{norm}_{2\pi}\big(\, s \cdot p \cdot \theta_m \;-\; \theta_0 \,\big)
$$

| Symbol | Meaning |
|---|---|
| p | pole pairs (known or estimated) |
| s ∈ {+1, −1} | sensor direction relative to the motor's electrical rotation |
| θ₀ | `zero_electric_angle`, the unknown offset |

Calibration must therefore find **s**, verify **p**, and measure **θ₀**.

---

## 2. Derivation of the alignment vector

SimpleFOC applies voltage through the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During alignment, SimpleFOC sets U_d = 0 and U_q = V_align. The resulting stator vector then points at angle θ + π/2.

We want the stator field on the **α-axis (phase A, electrical 0)**. That requires:

$$
\theta + \tfrac{\pi}{2} \equiv 0 \;\Rightarrow\; \theta = \tfrac{3\pi}{2}
$$

The magic constant `_3PI_2` in SimpleFOC comes from this. Applying `setPhaseVoltage(V_align, 0, _3PI_2)` produces a stationary field along phase A. The rotor's permanent magnet (its d-axis) snaps into alignment with that field, so at rest the **true electrical angle is 0**.

If we read the sensor in that state and require θₑ = 0:

$$
0 = s\,p\,\theta_m^{(\text{aligned})} - \theta_0
\;\Rightarrow\;
\boxed{\theta_0 = \text{norm}\big(s\,p\,\theta_m^{(\text{aligned})}\big)}
$$

---

## 3. Direction and pole-pair detection

Before measuring θ₀, SimpleFOC finds **s** by driving an open-loop electrical rotation and watching which way the sensor moves.

1. **Sweep forward.** Step the commanded angle θ_k = 3π/2 + 2π·k/500 for k = 0…500, which is one full electrical revolution. Then read θ_mid.
2. **Sweep back.** Step k = 500…0 and read θ_end.
3. **Set the direction.**
   - If θ_mid < θ_end, the sensor counts opposite to the electrical rotation: s = −1 (`Direction::CCW`).
   - Otherwise s = +1 (`Direction::CW`).
4. **Check pole pairs.** One electrical revolution equals 1/p of a mechanical revolution:

$$
\Delta\theta_m = |\theta_{mid} - \theta_{end}| \approx \frac{2\pi}{p}
\quad\Rightarrow\quad
p_{\text{est}} = \frac{2\pi}{\Delta\theta_m}
$$

   SimpleFOC warns if |Δθₘ·p − 2π| > 0.5 rad. That warning means the configured pole-pair count is wrong or the rotor stalled or slipped.

---

## 4. Full algorithm (SimpleFOC-equivalent)

```cpp
int BLDCMotor::alignSensor() {
  // ---- 1. Direction detection ----
  if (sensor_direction == Direction::UNKNOWN) {
    for (int i = 0; i <= 500; i++) {                  // forward 1 electrical rev
      float a = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, a);
      sensor->update();
      _delay(2);
    }
    sensor->update();
    float mid_angle = sensor->getAngle();

    for (int i = 500; i >= 0; i--) {                  // back again
      float a = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, a);
      sensor->update();
      _delay(2);
    }
    sensor->update();
    float end_angle = sensor->getAngle();
    setPhaseVoltage(0, 0, 0);
    _delay(200);

    if (mid_angle == end_angle) return 0;             // didn't move -> fail
    sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;

    float moved = fabs(mid_angle - end_angle);        // pole-pair sanity check
    if (fabs(moved * pole_pairs - _2PI) > 0.5f)
      SIMPLEFOC_DEBUG("PP check failed, estimated PP: ", _2PI / moved);
  }

  // ---- 2. Zero electrical angle ----
  if (!_isset(zero_electric_angle)) {
    setPhaseVoltage(voltage_sensor_align, 0, _3PI_2); // field on phase A
    _delay(700);                                      // let rotor settle
    sensor->update();
    zero_electric_angle = 0;                          // so electricalAngle() returns raw s*p*θm
    zero_electric_angle = electricalAngle();          // = norm(s*p*θm_aligned)
    setPhaseVoltage(0, 0, 0);
    _delay(200);
  }
  return 1;
}

float BLDCMotor::electricalAngle() {
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

To skip calibration on later boots, store the printed values and pass them in:

```cpp
motor.initFOC(zero_electric_angle, Direction::CW);
```

---

## 5. AS5600-specific details

**Basic setup**

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
// equivalent to: MagneticSensorI2C(0x36, 12, 0x0C, 4);
//                address, bits, RAW ANGLE reg (0x0C/0x0D), MSB bits used
```

**Angle conversion:** θₘ = raw · 2π / 4096.

**Electrical resolution degrades by p.** With a 7-pole-pair motor, the 0.088° mechanical step becomes about 0.62° electrical. That is fine for torque control but noticeable at low speed.

**Read RAW ANGLE (0x0C), not ANGLE (0x0E).** The ANGLE register is affected by the chip's ZPOS/MPOS/MANG programming. You do not need to burn ZPOS, because SimpleFOC's θ₀ handles the offset in software.

**Full rotations.** `Sensor::update()` unwraps the angle by detecting jumps larger than 0.8·2π between samples. This adds or subtracts a full turn, giving a multi-turn `getAngle()`. Alignment uses `getMechanicalAngle()`, which is single-turn, so wraparound does not affect θ₀.

**I²C speed.** At the default 100 kHz, a read takes about 0.3–0.5 ms, which limits the loop rate. Use `Wire.setClock(400000)` after `sensor.init()`.

---

## 6. Practical accuracy improvements

- **Alignment voltage:** choose V_align so the current is about 20–50 % of rated. Too low and cogging or friction leaves an offset error. Too high and the motor heats.
- **Average the offset:** take N readings after settling and average them. Use a circular mean (sum of sin/cos, then `atan2`) to avoid wrap-around errors near 0/2π.
- **Two-sided alignment:** approach the 3π/2 position from both directions and average the two offsets. This cancels friction and cogging hysteresis.
- **Magnet placement:** keep the magnet 0.5–3 mm from the chip, diametrically magnetized, and centered within about 0.25 mm. Eccentricity produces a once-per-revolution error that no single offset can remove. Fixing it requires a lookup-table calibration, such as SimpleFOC's `CalibratedSensor`.
- **Verify the result:** run `motor.controller = MotionControlType::torque` with a small target. The motor should spin equally well in both directions with minimal current. If it is clearly weaker or noisier in one direction, θ₀ is off by a few electrical degrees.