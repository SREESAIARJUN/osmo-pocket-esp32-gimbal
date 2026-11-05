# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Two corrections first

1. **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5048A/B and AS5047P (16384 counts/rev). The algorithm below is the same for both. Only the count-to-angle scaling and the resolution numbers change, and I show both.
2. **I don't know what "Cycle 26" refers to.** It isn't a SimpleFOC release name or a term I recognize. If it's from a course, a project tracker, or a fork, tell me what it requires and I can tailor this. What follows is the algorithm in the mainline SimpleFOC library (v2.x, `BLDCMotor::initFOC()` → `alignSensor()`).

---

## 1. Problem statement

FOC needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator α-axis (phase A).

The encoder only gives a **mechanical angle** θₘ, measured from an arbitrary magnet orientation.

The relationship is:

$$
\theta_e = \big[\, d \cdot p \cdot \theta_m \;-\; \theta_{0} \,\big] \bmod 2\pi
$$

| Symbol | Meaning | How it is found |
|---|---|---|
| p | pole pairs | user-supplied, sanity-checked |
| d ∈ {+1, −1} | sensor direction (CW/CCW) relative to electrical rotation | measured |
| θ₀ | `zero_electric_angle`: the electrical offset between the magnet's zero and the rotor d-axis | measured |

The calibration must therefore determine **d** and **θ₀**, and verify **p**.

---

## 2. Encoder reading (AS5600 specifics)

The AS5600 is read over I²C at address `0x36`. The RAW ANGLE register is `0x0C` (high nibble, bits 11:8) and `0x0D` (low byte).

$$
\theta_m^{raw} = \frac{\text{count}}{N}\,2\pi, \quad N = 4096 \;(\text{AS5600}) \;\text{or}\; 16384 \;(\text{AS5048/5047})
$$

**Full-rotation tracking.** `Sensor::update()` builds a continuous angle from the wrapped one by detecting jumps:

```
Δ = θ_now − θ_prev
if |Δ| > 0.8·2π :  full_rotations += (Δ > 0 ? −1 : +1)
θ_m = full_rotations·2π + θ_now
```

**Resolution in the electrical domain:**

$$
\Delta\theta_e = \frac{2\pi\,p}{N}
$$

For p = 7:
- AS5600: 0.088° mechanical → **0.62° electrical**
- AS5048: 0.022° mechanical → **0.15° electrical**

A static angle error ε reduces torque by a factor of cos ε. Even 1° of error costs only 0.015% of torque, so quantization is not the limiting factor. Mechanical magnet eccentricity and I²C read latency at speed matter far more.

---

## 3. Why the rotor locks at a known electrical angle

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` applies an inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During alignment, Ud = 0 and Uq = V_align (`voltage_sensor_align`). The stator field vector therefore points at angle **θ + π/2**.

An unloaded rotor settles with its d-axis (magnet flux) on the stator field. So when SimpleFOC "commands Uq at θ", the rotor's true electrical angle becomes θ + π/2.

SimpleFOC commands **θ = 3π/2**:

$$
U_\alpha = -V\sin(3\pi/2) = +V,\quad U_\beta = V\cos(3\pi/2) = 0
$$

The field points exactly along +α (phase A). The rotor d-axis locks there, which is by definition **θₑ = 0**.

---

## 4. Step A: direction detection and pole-pair check

The algorithm drives the field open-loop through one full electrical revolution and watches the sensor.

```
for i in 0..500:   θ = 3π/2 + 2π·i/500 ; setPhaseVoltage(V, 0, θ); sensor.update(); delay(2ms)
θ_mid = sensor.getAngle()
for i in 500..0:   (same, reversed)           // return to start
θ_end = sensor.getAngle()
setPhaseVoltage(0,0,0); delay(200)

moved = |θ_mid − θ_end|
d = (θ_mid > θ_end) ? CCW(−1) : CW(+1)        // SimpleFOC convention
```

- **Direction:** one electrical turn of the field drags the rotor forward one electrical turn. Whether the sensor angle increased or decreased gives d.
- **Pole-pair check:** one electrical turn equals 2π/p mechanical, so the expected movement is p · moved ≈ 2π. SimpleFOC warns when

$$
|p\cdot \text{moved} - 2\pi| > 0.5\ \text{rad}
$$

  Typical causes of a failed check are a wrong pole-pair count, too low a V_align (the rotor stalls or cogs), or a slipping magnet.
- **Movement check:** if moved is below a small threshold, the motor didn't move. Alignment fails and you should check the wiring and power supply.

---

## 5. Step B: zero-offset (θ₀) measurement

```
setPhaseVoltage(V_align, 0, 3π/2);   // field on +α  → true θ_e = 0
delay(700);                          // let rotor settle, oscillations die out
sensor.update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();  // = norm(d·p·θ_m)
setPhaseVoltage(0, 0, 0);
```

**Derivation.** At this instant the true electrical angle is 0. Substituting into the model from Section 1:

$$
0 = d\,p\,\theta_m^{*} - \theta_0 \;\Rightarrow\; \theta_0 = \big[d\,p\,\theta_m^{*}\big] \bmod 2\pi
$$

Here θ*ₘ is the sensor reading in the locked position. Setting `zero_electric_angle = 0` before calling `electricalAngle()` is what makes the function return exactly this expression.

At run time, every FOC loop then computes:

```cpp
float BLDCMotor::electricalAngle(){
  return _normalizeAngle( (float)(sensor_direction * pole_pairs)
                          * sensor->getMechanicalAngle() - zero_electric_angle );
}
```

The single-snapshot method has a weakness. Cogging torque and friction can leave the rotor a few electrical degrees off the field axis, and a single sample also captures sensor noise.

---

## 6. Improved variant (recommended)

This version averages over several positions and both approach directions. Errors that oppose the motion, such as friction, cancel when the two directions are combined.

```cpp
float calibrateZero(BLDCMotor& m, Sensor& s, float V, int K = 8) {
  float sx = 0, sy = 0;
  for (int k = 0; k < K; k++) {
    float target = 2*PI*k/K;                       // desired true θe
    for (int dir = -1; dir <= 1; dir += 2) {       // approach from both sides
      for (int j = 0; j <= 50; j++) {
        float th = target + dir*(0.5f - j*0.01f);  // sweep into target
        m.setPhaseVoltage(V, 0, th + _3PI_2);      // +3π/2 maps Uq-angle → field on θe
        delay(2);
      }
      delay(300);
      float acc = 0;
      for (int n = 0; n < 32; n++) { s.update(); acc += s.getMechanicalAngle(); delay(1); }
      float thm = acc/32;
      float off = _normalizeAngle(m.sensor_direction*m.pole_pairs*thm - target);
      sx += cosf(off); sy += sinf(off);            // circular mean (wrap-safe)
    }
  }
  m.setPhaseVoltage(0, 0, 0);
  return _normalizeAngle(atan2f(sy, sx));
}
```

- The **circular mean** avoids the 0/2π wraparound bug that an arithmetic mean has.
- Measuring K points spread across the electrical cycle also averages out first-order magnet eccentricity.
- The **spread** of the individual offsets is a diagnostic. If it exceeds about 10° electrical, the magnet is off-center or the p value is wrong.
- The sample averaging assumes the rotor isn't sitting exactly at the 0/2π mechanical wrap. If it might be, average the readings with a circular mean as well.

---

## 7. Practical notes

- **Choosing V_align:** pick it so that the phase current is roughly 20–50% of rated current: V ≈ I·R_phase. If it is too low, cogging dominates. If it is too high, the motor heats and the rotor overshoots