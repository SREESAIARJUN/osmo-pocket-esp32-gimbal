# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## A Correction on Resolution

The **AS5600 is a 12-bit encoder** (4096 counts per revolution), not 14-bit. The 14-bit parts in the same family are the AS5047P/D, AS5048A/B and AS5147. The derivation below works for any absolute magnetic encoder. Only the resolution numbers in Section 5 change: 12-bit gives 0.088° mechanical per count, 14-bit gives 0.022°.

I don't recognise "Cycle 4" as a standard SimpleFOC term. If it refers to a stage in your course or project, the calibration below is the `initFOC()` → `alignSensor()` phase.

---

## 1. The Problem

FOC needs the rotor's **electrical angle** θₑ, measured relative to the stator α-axis (phase A). The encoder reports a **mechanical angle** θₘ, measured from an arbitrary magnet orientation. They are related by:

$$
\theta_e = \text{norm}_{[0,2\pi)}\big(d \cdot p \cdot \theta_m - \theta_0\big)
$$

where:
- **p** is the number of pole pairs,
- **d** is the sensor direction (±1, i.e. whether encoder CW matches motor-phase CW),
- **θ₀** is `zero_electric_angle`, the unknown offset.

Calibration must determine **d** and **θ₀**, and optionally verify **p**.

---

## 2. Why SimpleFOC Uses θ = 3π/2

`setPhaseVoltage(Uq, Ud, θ)` applies an inverse Park transform. With Ud = 0:

$$
U_\alpha = -U_q \sin\theta, \qquad U_\beta = U_q \cos\theta
$$

The resulting stator voltage vector therefore points at angle **θ + π/2**, because a pure q-axis voltage leads the d-axis by 90°.

Substituting θ = 3π/2:

$$
U_\alpha = -U_q\sin(3\pi/2) = +U_q, \qquad U_\beta = U_q\cos(3\pi/2) = 0
$$

The stator field points exactly along **α (phase A)**.

With no load, the rotor's d-axis (its magnet flux) aligns with the stator field. That is the minimum-energy position, where torque ∝ sin(misalignment) = 0. At equilibrium, the true rotor electrical angle is therefore:

$$
\theta_e^{\text{true}} = 0
$$

---

## 3. Deriving θ₀

At the locked position, the encoder reads θₘ*. Setting θₑ = 0 in the model gives:

$$
0 = d \cdot p \cdot \theta_m^* - \theta_0 \;\Rightarrow\; \boxed{\theta_0 = \text{norm}\big(d \cdot p \cdot \theta_m^*\big)}
$$

This is exactly what SimpleFOC computes:

```cpp
zero_electric_angle = _normalizeAngle(_electricalAngle(sensor_direction * sensor->getAngle(), pole_pairs));
```

---

## 4. Direction Detection (Done Before the Zero Step)

The direction **d** must be known before θ₀ is valid, so it is found first:

1. Sweep the commanded electrical angle open-loop from 3π/2 to 3π/2 + 2π, in 500 steps, using `voltage_sensor_align`. Record θ_mid.
2. Sweep back to 3π/2. Record θ_end.
3. Compute Δ = θ_mid − θ_end.
4. Interpret the result:
   - If Δ > 0, then **d = CW (+1)**.
   - If Δ < 0, then **d = CCW (−1)**.
   - If |Δ| is approximately 0, the motor didn't move. Check the wiring, supply voltage and `voltage_sensor_align`.

**Pole-pair check:** one electrical revolution should move the shaft 2π/p mechanically. Estimate:

$$
\hat p = \frac{2\pi}{|\Delta|}
$$

If |p̂ − p| > 0.5, SimpleFOC warns with "PP check: fail".

---

## 5. Error Budget for the AS5600

| Source | Mechanical error | Electrical error (×p, e.g. p = 7) |
|---|---|---|
| Quantization (12-bit) | ±0.044° | ±0.31° |
| AS5600 INL (typical) | up to ~±1° | up to ~±7° |
| Cogging / friction offset at lock | 0.1–1° | 0.7–7° |
| Magnet eccentricity | varies | can dominate |

The torque efficiency lost to an offset error ε is:

$$
\eta = \cos(\varepsilon)
$$

For ε = 7°, η ≈ 0.993, a 0.7% torque loss. Small offset errors are tolerable.

The bigger symptom of a wrong offset is **asymmetric speed in the two directions**, because an offset error acts as a spurious d-axis current component.

---

## 6. Improved Algorithm: Bidirectional Averaging

Friction makes the rotor stop slightly *short* of the true equilibrium in whichever direction it approached from. Averaging approaches from both sides cancels this lag to first order:

```cpp
float calibrateZero(BLDCMotor& m, Sensor& s, int N = 8) {
  float sx = 0, sy = 0;                       // circular mean (handles wrap)
  for (int i = 0; i < N; i++) {
    float approach = (i % 2) ? +_PI_2 : -_PI_2;   // alternate sides
    for (int k = 0; k <= 100; k++) {              // slew into 3π/2
      m.setPhaseVoltage(m.voltage_sensor_align, 0,
                        _3PI_2 + approach * (1.0f - k / 100.0f));
      delay(2);
    }
    delay(300);                                    // settle
    for (int k = 0; k < 20; k++) { s.update(); delay(1); }  // AS5600 I2C refresh
    float th0 = _normalizeAngle(m.sensor_direction * m.pole_pairs * s.getMechanicalAngle());
    sx += cosf(th0); sy += sinf(th0);
  }
  m.setPhaseVoltage(0, 0, 0);
  return _normalizeAngle(atan2f(sy, sx));
}
// motor.zero_electric_angle = calibrateZero(motor, sensor);
// motor.initFOC();   // skips alignment when zero_electric_angle and sensor_direction are preset
```

**Key points:**

- **Use a circular mean (atan2 of summed sin/cos), not an arithmetic mean.** An arithmetic mean fails when samples straddle 0/2π.
- **Use `getMechanicalAngle()`, not `getAngle()`.** `getAngle()` includes full-rotation tracking. It cancels after normalization anyway, but the mechanical angle is cleaner.

---

## 7. AS5600 Practical Notes

- **Bus speed:** run I²C at 400 kHz or faster. Alternatively, use the analog/PWM OUT pin via `MagneticSensorAnalog` or `MagneticSensorPWM`, though PWM adds latency and jitter.
- **Leave ZPOS/MPOS alone:** don't program them for FOC. SimpleFOC's θ₀ handles the offset in software, and burning OTP is irreversible.
- **Magnet setup:** use a diametric magnet with a 0.5–3 mm air gap. Check that the MD bit is set and ML/MH are clear (STATUS register 0x0B).
- **Calibration voltage:** set `voltage_sensor_align` high enough to overcome cogging, typically 1–3 V for gimbal motors. Keep it low enough to avoid overheating during the hold.
- **Skip calibration on later boots:** store `zero_electric_angle` and `sensor_direction` in EEPROM/flash. This only works if the magnet is never remounted.

Two quick checks after calibration:
- **Offset check:** spin the motor in closed-loop velocity mode at ±same voltage. The speeds should match within a few %. A large asymmetry means θ₀ is wrong.
- **Pole-pair check:** an "MOT: PP check: fail" message means **p** is wrong or the encoder is slipping.