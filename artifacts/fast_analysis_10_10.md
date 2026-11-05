# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

**A correction first:** the AS5600 is a **12-bit** encoder (4096 counts/rev). The 14-bit parts are the AS5047P/AS5048A/B. The derivation below is resolution-agnostic, and the AS5600-specific numbers use 12 bits.

I don't know what "Cycle 10" refers to. It isn't a SimpleFOC version or a term I recognize. If it's a step in your own project, the derivation still applies. If it refers to a specific library release, check `BLDCMotor::alignSensor()` in that version against the code below.

---

## 1. The problem

FOC needs the rotor's **electrical angle** θₑ, the angle of the rotor's d-axis (magnet flux) relative to the stator's α-axis (phase A). The sensor gives a **mechanical angle** θₘ with an arbitrary zero, wherever the magnet happened to be glued. These are related by:

$$
\theta_e = \text{wrap}_{2\pi}\big(\, d \cdot p \cdot \theta_m \;-\; \theta_{0}\,\big)
$$

| Symbol | Meaning |
|---|---|
| p | pole pairs (`motor.pole_pairs`) |
| d ∈ {+1, −1} | sensor direction relative to the electrical rotation (`sensor_direction`) |
| θ₀ | electrical zero offset (`zero_electric_angle`) |

This is exactly SimpleFOC's `electricalAngle()`:
```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```
Calibration must determine **d** and **θ₀**, and sanity-check **p**.

---

## 2. Why SimpleFOC aligns at 3π/2

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` uses the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

Calibration calls `setPhaseVoltage(U_align, 0, 3π/2)`, i.e. U_q = U_align, U_d = 0, θ = 3π/2:

$$
U_\alpha = -U_{align}\cdot\sin(3\pi/2) = +U_{align},\qquad U_\beta = U_{align}\cdot\cos(3\pi/2)=0
$$

The stator field therefore points **along the +α axis**. With no load, the rotor's magnetic d-axis aligns with the stator field, which is the minimum-energy position. At that instant the true electrical angle is, by definition:

$$
\theta_e^{true} = 0
$$

Using U_q at 3π/2 rather than U_d at 0 is a convention choice. It produces the same vector and keeps one code path (voltage-mode q-axis) for both the sweep and the lock.

---

## 3. Deriving the offset

At the locked position the sensor reads θₘ\*. Requiring the model to output zero:

$$
0 = d\,p\,\theta_m^* - \theta_0 \;\;\Rightarrow\;\; \boxed{\theta_0 = \text{wrap}_{2\pi}(d\,p\,\theta_m^*)}
$$

SimpleFOC implements this by setting `zero_electric_angle = 0` and then calling `electricalAngle()`, which returns exactly d·p·θₘ\* wrapped.

**Order dependence:** θ₀ depends on d, so **direction must be found first**.

---

## 4. Direction detection (and pole-pair check)

Open-loop sweep the electrical angle forward one full electrical revolution, then backward:

$$
\theta_e^{cmd}: \tfrac{3\pi}{2} \to \tfrac{3\pi}{2}+2\pi \quad(\text{forward}),\qquad \text{then back}
$$

The rotor follows the field, so the mechanical angle moves by Δθₘ = 2π/p.

- Record θₘ at the end of the forward sweep (θ_mid) and at the end of the backward sweep (θ_end).
- If θ_mid > θ_end, the sensor increases when electrical angle increases, so d = +1 (`CW` in SimpleFOC naming). Otherwise d = −1 (`CCW`).
- **Pole-pair check:** |Δθₘ| · p should equal 2π:

$$
\big|\,|\theta_{mid}-\theta_{end}|\cdot p - 2\pi\,\big| < 0.5 \;\Rightarrow\; p \text{ OK}
$$

Use `getAngle()` (multi-turn, continuous) and not the wrapped angle. Otherwise the difference breaks across the 0/2π seam.

---

## 5. Reference implementation (mirrors `BLDCMotor::alignSensor`)

```cpp
int alignSensor(float U) {
  // ---- 5a. Direction ----
  for (int i = 0; i <= 500; i++) {                  // forward sweep
    float a = _3PI_2 + _2PI * i / 500.0f;
    setPhaseVoltage(U, 0, a);
    sensor->update();
    _delay(2);
  }
  sensor->update();
  float mid = sensor->getAngle();

  for (int i = 500; i >= 0; i--) {                  // backward sweep
    float a = _3PI_2 + _2PI * i / 500.0f;
    setPhaseVoltage(U, 0, a);
    sensor->update();
    _delay(2);
  }
  sensor->update();
  float end = sensor->getAngle();
  setPhaseVoltage(0, 0, 0);
  _delay(200);

  float moved = fabs(mid - end);
  if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;  // rotor didn't move -> fail
  sensor_direction = (mid < end) ? Direction::CCW : Direction::CW;

  if (fabs(moved * pole_pairs - _2PI) > 0.5f)
    SIMPLEFOC_DEBUG("MOT: PP check fail, est. pp=", _2PI / moved);

  // ---- 5b. Zero offset ----
  setPhaseVoltage(U, 0, _3PI_2);                    // field on +alpha
  _delay(700);                                      // let rotor settle
  sensor->update();
  zero_electric_angle = 0;
  zero_electric_angle = electricalAngle();          // = wrap(d*p*theta_m*)
  _delay(20);
  setPhaseVoltage(0, 0, 0);
  _delay(200);
  return 1;
}
```

---

## 6. AS5600-specific considerations

**Setup:**
```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);  // addr 0x36, RAW ANGLE reg 0x0C, 12 bit
Wire.setClock(400000);                                     // default 100 kHz is too slow for FOC
sensor.init();
motor.linkSensor(&sensor);
```

**Resolution in the electrical domain:**

$$
\Delta\theta_e = \frac{2\pi p}{4096}
$$

For a 7-pole-pair gimbal motor this is about 0.0107 rad (0.62° electrical) per count, which is fine. Noise of roughly ±1–2 LSB adds about ±1° electrical of jitter to θ₀ from a single sample.

**Practical pitfalls:**
- **Magnet status:** read register 0x0B (bit 5 MD = magnet detected, bit 4 ML = too weak, bit 3 MH = too strong) before calibrating. A bad air gap causes nonlinearity, which shows up as position-dependent offset error.
- **I²C latency:** about 100–200 µs per read at 400 kHz. This limits loop rate, but it doesn't affect static calibration.
- **DIR pin:** float it and it flips d at random. Tie it to GND or VCC.
- **Alignment voltage:** `voltage_sensor_align` must overcome cogging and friction but not overheat the motor. 1–3 V is typical for gimbal motors. For low-resistance drones/hoverboard motors, keep current I = U/R safe.

---

## 7. Improved zero-finding: multi-point circular mean

A single lock point inherits cogging error, friction-induced lag, and sensor nonlinearity. A better estimate locks at N electrical angles θₖ = 2πk/N and computes each residual: