# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Corrections to the question

- **The AS5600 is a 12-bit encoder (4096 counts per revolution), not 14-bit.** The 14-bit parts in the same family are the AS5047P, AS5048A/B and AS5147. The derivation below is resolution-independent, so only the constants change.
- **I don't recognize "Cycle 20"** as a SimpleFOC term, version, or AS5600 feature. If it refers to a course module or a specific code revision, let me know and I can tailor the answer. Below is the algorithm as implemented in SimpleFOC 2.x (`BLDCMotor::initFOC()` → `alignSensor()`).

---

## 1. What has to be calibrated

FOC needs the rotor's **electrical** angle θₑ relative to the stator's α-axis (phase A). The sensor gives a **mechanical** angle θₘ with an arbitrary mounting offset. The relationship is:

$$
\theta_e = \big[\, d \cdot p \cdot \theta_m \;-\; \theta_0 \,\big] \bmod 2\pi
$$

| Symbol | Meaning | SimpleFOC variable |
|---|---|---|
| p | pole pairs | `pole_pairs` |
| d ∈ {+1, −1} | sensor direction relative to the electrical rotation | `sensor_direction` (CW/CCW) |
| θ₀ | electrical zero offset | `zero_electric_angle` |

This matches the code:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

Calibration means finding **d**, verifying **p**, and finding **θ₀**.

---

## 2. Key fact: how an open-loop voltage vector is placed

`setPhaseVoltage(Uq, Ud, angle_el)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

With U_d = 0, the stator vector points at θ + π/2. A large, static stator field pulls the rotor's magnetic d-axis (flux axis) onto itself.

So commanding `setPhaseVoltage(U_align, 0, 3π/2)` produces a field at 3π/2 + π/2 = 2π ≡ 0, which is the α-axis (phase A).

**Result: the rotor's d-axis locks to electrical angle 0.** Whatever the sensor reads at that moment is, by definition, the zero offset.

---

## 3. Step A: direction detection (find d)

Sweep the field one full electrical revolution forward, then back, and see which way the sensor moved:

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
if (moved < MIN_ANGLE_DETECT_MOVEMENT) { /* fail: rotor stuck / no sensor */ }
else if (mid_angle < end_angle) sensor_direction = Direction::CCW;   // d = -1
else                            sensor_direction = Direction::CW;    // d = +1
```

**Logic:**
- Positive electrical rotation should give increasing θₘ when d = +1.
- If the forward sweep *decreased* the reading (mid < end after returning), the sensor counts opposite to the phase sequence, so d = −1.
- The slow ramp (≈1 s per direction) keeps the rotor quasi-statically locked, so it doesn't slip a pole.

---

## 4. Step B: pole-pair sanity check

One electrical revolution equals 2π/p mechanical radians. So:

$$
p_{\text{est}} = \frac{2\pi}{\Delta\theta_m}
\quad\Rightarrow\quad
\text{check }\;|\Delta\theta_m \cdot p - 2\pi| < 0.5\ \text{rad}
$$

```cpp
if (fabs(moved * pole_pairs - _2PI) > 0.5f) {
  // warning: PP check failed, estimated PP = _2PI / moved
}
```

This is only a warning in SimpleFOC. If it fails, fix `pole_pairs`, or check whether cogging or a heavy load caused slip.

---

## 5. Step C: zero offset (find θ₀)

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);  // field at electrical 0
_delay(700);                                       // let rotor settle
sensor->update();
zero_electric_angle = 0;                           // so electricalAngle() returns the raw value
zero_electric_angle = electricalAngle();           // = norm(d·p·θm_locked)
_delay(20);
setPhaseVoltage(0, 0, 0);
```

**Derivation:** at lock the true θₑ = 0, so

$$
0 = d\,p\,\theta_{m,\text{lock}} - \theta_0
\;\Rightarrow\;
\theta_0 = (d\,p\,\theta_{m,\text{lock}}) \bmod 2\pi
$$

Setting `zero_electric_angle = 0` first makes `electricalAngle()` return exactly that expression.

**Note on the rotor angle used:** the code uses `getMechanicalAngle()` (wrapped to [0, 2π)), not the multi-turn `getAngle()`. Because p is an integer, the multiples of 2π from full turns vanish under the modulo, so θ₀ is valid at any absolute rotor position.

---

## 6. AS5600-specific details

| Item | Value |
|---|---|
| Interface | I²C address 0x36; RAW ANGLE register 0x0C/0x0D (12 bits); also PWM/analog output |
| CPR | 4096 → mechanical resolution 2π/4096 ≈ 0.088° |
| Electrical resolution | p × 0.088° (e.g. 7 pp → ≈0.62° electrical) |
| SimpleFOC object | `MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);` |
| Bus speed | Use 400 kHz (`Wire.setClock(400000)`); at 100 kHz the reads limit the loop rate |

**Error budget for θ₀:**

$$
\delta\theta_0 = p\,(\delta_{\text{quant}} + \delta_{\text{noise}} + \delta_{\text{lock}})
$$

- `δ_lock` comes from cogging and friction. The rotor doesn't sit exactly on the field axis.
- Typical AS5600 noise is a few LSB, so a single-sample θ₀ can be off by a few electrical degrees on high-pole-count gimbal motors.
- A few degrees of error is benign: torque scales with cos(error), and the error leaks a small d-axis component.

---

## 7. Practical improvements

1. **Average the lock reading.** Take N samples after settling. Average them with a circular mean (sin/cos), not arithmetic, to survive the 0/2π wrap.
2. **Approach from both directions.** Lock at electrical 0 from a forward ramp and from a backward ramp, then average. This cancels friction/cogging hysteresis.
3. **Use enough voltage.** Choose `voltage_sensor_align` high enough to beat cogging, but keep current safe for the winding resistance.
4. **Check the magnet.** Use the AS5600 STATUS register (MD/ML/MH bits) to confirm magnet strength before calibrating.
5. **Skip calibration at boot once the values are known.** Read `motor.zero_electric_angle` and `motor.sensor_direction` from the monitor output, then hard-code them:

```cpp
motor.zero_electric_angle = 2.15f;