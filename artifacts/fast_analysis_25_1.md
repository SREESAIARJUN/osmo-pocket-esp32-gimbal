# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## First, two corrections

1. **The AS5600 is a 12-bit encoder, not 14-bit.** It gives 4096 counts per revolution, about 0.088° per count. The 14-bit parts in the same family are the AS5047P/D and AS5048A/B, which give 16384 counts per revolution. This matters for the error budget below.
2. **I don't recognise "Cycle 25"** as a SimpleFOC release, milestone, or documentation term. The derivation below follows the alignment algorithm in current SimpleFOC (`BLDCMotor::initFOC()` → `alignSensor()`). It has been stable across the 2.x releases. If "Cycle 25" refers to a specific course, fork, or document, let me know and I can adapt this to it.

---

## 1. What needs to be calibrated

FOC needs the rotor's **electrical** angle θₑ, meaning the position of the rotor flux (d-axis) relative to the stator phase-A axis. The sensor gives a **mechanical** angle θₘ with an arbitrary mounting offset. The relationship is:

$$
\theta_e = \big(d \cdot p \cdot \theta_m - \theta_0\big) \bmod 2\pi
$$

| Symbol | Meaning | How it is found |
|---|---|---|
| p | pole pairs | known, or estimated |
| d ∈ {+1, −1} | sensor direction relative to the phase sequence | `sensor_direction` (CW/CCW) |
| θ₀ | electrical zero offset | `zero_electric_angle` |

Calibration therefore means finding **d** and **θ₀**, and optionally checking **p**.

## 2. Forcing a known electrical angle (open-loop alignment)

SimpleFOC applies voltage through the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During alignment it calls `setPhaseVoltage(Uq = voltage_sensor_align, Ud = 0, θ)`.

With θ = 3π/2:

$$
U_\alpha = -U_q\sin(3\pi/2) = +U_q,\qquad U_\beta = U_q\cos(3\pi/2)=0
$$

The stator field then points exactly along the α axis (phase A). The rotor's permanent-magnet d-axis aligns with this field and settles there. By definition, that is **electrical angle 0** in the d-q frame used by the controller.

This is why `_3PI_2` appears in the code. Applying "pure q-voltage at 3π/2" is the same as applying pure d-voltage at 0.

## 3. Step A: direction detection

1. Sweep the commanded electrical angle forward through one electrical revolution, then back. SimpleFOC uses 500 steps each way.
2. Compare the mechanical angle before and after the forward sweep:

$$
\Delta\theta_m = \theta_m^{end} - \theta_m^{mid}
$$

3. Decide the direction:
   - Δθₘ > 0 → `sensor_direction = CW` (d = +1)
   - Δθₘ < 0 → `sensor_direction = CCW` (d = −1)
   - |Δθₘ| ≈ 0 → failure. Usual causes are no movement, voltage too low, or a sensor read problem.

## 4. Step B: pole-pair sanity check

One electrical revolution should move the rotor 2π/p mechanically. SimpleFOC estimates:

$$
\hat p = \frac{2\pi}{|\Delta\theta_m|}
$$

It flags a mismatch if |p̂ − p| > 0.5.

## 5. Step C: finding the zero offset

1. Apply the alignment vector at electrical 0, i.e. `setPhaseVoltage(U_align, 0, 3π/2)`.
2. Wait about 700 ms so the rotor stops oscillating.
3. Call `sensor->update()` and read θₘ*.
4. At this pose the true electrical angle is 0, so:

$$
0 = d\,p\,\theta_m^* - \theta_0 \;\Rightarrow\; \boxed{\theta_0 = (d\,p\,\theta_m^*) \bmod 2\pi}
$$

In code this is `zero_electric_angle = electricalAngle()`, evaluated while `zero_electric_angle` is still 0.

5. Set the phase voltages to 0 and finish.

From then on, `electricalAngle()` returns `_normalizeAngle(d·p·θₘ − θ₀)`.

## 6. Error budget for the AS5600

The electrical angle error is the mechanical error multiplied by the number of pole pairs:

$$
\delta\theta_e = p\cdot\delta\theta_m
$$

- **Quantization:** δθₘ = 360°/4096 ≈ 0.088°.
  - For a gimbal motor with p = 7, this gives about 0.62° electrical. That is negligible.
  - For p = 21, it gives about 1.8° electrical. Still acceptable.
- **The real error sources are larger:**
  - **Static friction and cogging** stop the rotor short of true alignment, often by several electrical degrees.
  - **Magnet eccentricity and axial gap.** The AS5600 needs a field of 30–90 mT, so check the MD/ML/MH status bits.
  - **Hysteresis and filtering.** Configure the CONF register: no slow filter, hysteresis OFF during calibration.

Torque loss from an offset error δ follows cos δ. Even 10° electrical costs only about 1.5% of torque, but it also adds cross-coupling into the d-axis.

## 7. Improved procedure (bidirectional averaging)

This cancels friction and cogging bias, which is the dominant error on cheap gimbal motors.

```cpp
// Run after a normal initFOC() has determined direction
float calibrateZero(BLDCMotor& m, int N = 8) {
  float sx = 0, sy = 0;
  for (int k = 0; k < N; k++) {
    for (int dir = -1; dir <= 1; dir += 2) {
      // approach electrical 0 from alternating sides
      for (int i = 0; i <= 100; i++) {
        float a = _3PI_2 + dir * (1.0f - i / 100.0f) * _PI_2;
        m.setPhaseVoltage(m.voltage_sensor_align, 0, a);
        _delay(2);
      }
      _delay(500);
      m.sensor->update();
      float th = _normalizeAngle(m.sensor_direction * m.pole_pairs
                                 * m.sensor->getMechanicalAngle());
      sx += cosf(th);  sy += sinf(th);   // circular mean avoids 0/2π wrap
    }
  }
  m.setPhaseVoltage(0, 0, 0);
  return _normalizeAngle(atan2f(sy, sx));
}
```

Store the result as `motor.zero_electric_angle` and skip the alignment on later boots:

```cpp
motor.zero_electric_angle = 2.1453f;   // from calibration
motor.sensor_direction    = Direction::CW;
motor.initFOC();                       // now skips alignment
```

## 8. AS5600 setup in SimpleFOC

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C); // addr 0x36, raw angle reg 0x0C, 12 bit
void setup() {
  Wire.setClock(400000);          // 1 MHz fast-mode+ if MCU supports it
  sensor.init();
  motor.linkSensor(&sensor);
  motor.voltage_sensor_align = 3; // V; enough to overcome cogging, not overheat
  motor.init();
  motor.initFOC();
}
```

A full I²C read takes roughly 100–200 µs, which limits the loop rate. If you need faster updates, use the AS5600's analog output with `MagneticSensorAnalog`, or its PWM output.

---

## Summary

| Step | What it does |
|---|---|
| 1 | Force the field to electrical 0 using q-voltage at 3π/2 (equivalent to pure α-axis voltage) |
| 2 | Sweep one electrical revolution and use the sign of Δθₘ to get the direction |
| 3 | Use \|Δθₘ\| to check the pole-pair count |
| 4 | Lock at electrical 0, read θₘ*, and set θ₀ = d·p·θₘ* mod 2π |
| 5 | Optionally take a bidirectional circular mean to remove friction bias |

On the AS5600, the sensor's quantization is not the limit. Mechanical alignment accuracy is.