# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

**Two clarifications first:**
- **The AS5600 is a 12-bit sensor** (4096 counts/rev), not 14-bit. The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The derivation below is the same for all of them; only the resolution numbers change.
- **I don't recognize "Cycle 16."** If it refers to a specific course, a lecture series, or a SimpleFOC release, tell me and I can align the answer to it. Below is the algorithm as implemented in SimpleFOC's `BLDCMotor::initFOC()` → `alignSensor()` (v2.x).

---

## 1. What problem are we solving?

FOC needs the **electrical rotor angle** θₑ, measured from the stator α-axis (phase A) to the rotor d-axis (magnet north). The encoder only provides a **mechanical angle** θₘ with an arbitrary mounting zero. The relationship is:

$$
\theta_e = \big(s \cdot p \cdot \theta_m - \theta_{e,0}\big) \bmod 2\pi
$$

| Symbol | Meaning | SimpleFOC variable |
|---|---|---|
| p | pole pairs | `pole_pairs` |
| s ∈ {+1, −1} | sensor direction relative to the electrical phase sequence | `sensor_direction` |
| θₑ,₀ | electrical offset (unknown) | `zero_electric_angle` |

Calibration must find **s** and **θₑ,₀**, and should verify **p**.

---

## 2. Forcing a known rotor position

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During alignment, the library calls `setPhaseVoltage(V_align, 0, 3π/2)`. With U_d = 0 and θ = 3π/2:

- sin(3π/2) = −1 and cos(3π/2) = 0
- so **U_α = V_align** and **U_β = 0**

The stator voltage, and in steady state the current and flux, therefore points exactly along the **α-axis**.

With current along α, the PMSM torque is:

$$
T = \tfrac{3}{2}\, p\, \psi_m\, i_\alpha \sin(0 - \theta_e) = -\tfrac{3}{2}\, p\, \psi_m\, i_\alpha \sin\theta_e
$$

This torque has a stable equilibrium at **θₑ = 0**: the magnet's d-axis locks onto phase A.

So once the rotor settles, the true electrical angle is 0 by construction. Substituting θₑ = 0 into the mapping gives:

$$
\boxed{\theta_{e,0} = \big(s\cdot p\cdot \theta_m^{\text{settled}}\big) \bmod 2\pi}
$$

This is why the code temporarily zeroes `zero_electric_angle` and then simply calls `electricalAngle()`.

> **Caveat:** the equilibrium is unique per electrical period. Mechanically there are p equivalent positions, but all give the same θₑ,₀ mod 2π, so it doesn't matter which one the rotor lands in.

---

## 3. Direction detection and pole-pair check

Before finding the zero, SimpleFOC rotates the field open-loop through one electrical revolution and watches which way the sensor moves.

```
for i in 0..500:  θ = 3π/2 + 2π·i/500 ; setPhaseVoltage(V,0,θ); sensor.update(); delay 2ms
mid = sensor.getAngle()
for i in 500..0:  (same, backwards)
end = sensor.getAngle()
setPhaseVoltage(0,0,0); delay 200ms

moved = |mid − end|
if moved < MIN_ANGLE_DETECT_MOVEMENT → fail ("no movement")
s = (mid < end) ? CCW(−1) : CW(+1)
if | moved·p − 2π | > 0.5 → warn ("PP check failed")
```

The reasoning:

- One electrical revolution corresponds to a mechanical rotation of Δθₘ = 2π / p.
- **Direction:** the field rotates in the positive electrical direction. If the sensor angle *increased*, then s = +1; if it decreased, s = −1.
- **Pole-pair check:** the product p·|Δθₘ| should equal 2π. The tolerance of 0.5 rad of electrical angle absorbs cogging and the lag of the open-loop sweep.
- **Why sweep forward then back:** comparing the forward endpoint against the return endpoint cancels static friction and backlash bias.

---

## 4. Zero-finding (`alignSensor`, second half)

```cpp
// 1. Lock rotor on α-axis
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                       // let mechanical transients die out
sensor->update();                  // AS5600: I2C read of RAW ANGLE 0x0C/0x0D

// 2. Measure offset
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();
//  = _normalizeAngle(sensor_direction * pole_pairs * sensor->getMechanicalAngle())

// 3. Release
setPhaseVoltage(0, 0, 0);
_delay(200);
```

To skip calibration on later boots, store the results and pass them back:

```cpp
motor.initFOC(zero_electric_angle, sensor_direction);
```

---

## 5. AS5600-specific details

**Reading the sensor.** Use `MagneticSensorI2C sensor(AS5600_I2C)`. This configuration has:
- `cpr = 4096`
- angle register `0x0C`, 12 data bits
- mechanical angle computed as `raw / 4096 · 2π`

**Resolution.** The AS5600's resolution is 0.088° mechanical, which becomes **p × 0.088° electrical**. For a 7-pole-pair gimbal motor that is about **0.62° electrical**. This is adequate: the resulting torque loss is cos(0.62°) ≈ 0.99994.

**Effect of offset error.** An error ε in θₑ,₀ splits the commanded current between the two axes:

$$
i_q^{\text{eff}} = i_q\cos\varepsilon,\qquad i_d^{\text{parasitic}} = i_q\sin\varepsilon
$$

Small errors cost very little, but large errors matter:
- ε ≈ 10° gives about 1.5% torque loss plus noticeable d-axis current, which shows up as heating and asymmetric CW/CCW speed.
- ε ≥ 90° produces runaway or no torque.

**I²C latency.** Each I²C read takes about 100–200 µs at 400 kHz, and the AS5600 output also includes a small digital filter delay. This does not matter during calibration, because the rotor is stationary when the sample is taken.

**Multi-turn tracking.** `Sensor::update()` adds or subtracts a full rotation when it sees a jump of |Δ| > 0.8·2π between reads. Calibration only uses `getMechanicalAngle()`, which is the single-turn value, so rotation tracking does not affect θₑ,₀.

**Hardware ZPOS register.** The AS5600 has a ZPOS register (0x01/0x02), but SimpleFOC does not use it. The software offset is equivalent and avoids burning OTP memory.

---

## 6. Practical refinements

1. **Average several samples** while the rotor is locked, using a circular mean so the 0/2π wrap doesn't corrupt the result:
   ```cpp
   float sx=0, sy=0;
   for(int i=0;i<32;i++){ sensor->update();
     float e = sensor_direction*pole_pairs*sensor->getMechanicalAngle();
     sx += cosf(e); sy += sinf(e); _delay(2); }
   zero_electric_angle = _normalizeAngle(atan2f(sy, sx));
   ```
2. **Two-sided alignment** cancels the bias caused by cogging and friction:
   - Approach θₑ = 0 once from +δ and once from −δ.
   - Take the circular mean of the two readings.
3. **Choose `voltage_sensor_align` carefully.** It must be high enough to overcome cogging torque, but low enough that the alignment current (V/R) stays safe for the motor. A typical choice is the voltage that produces about 30–50% of rated current.
4. **Keep the AS5600 magnet within spec.** The air gap should be 0.5–3 mm and the magnet must be diametrically magnetized. Check the `MD`/`ML`/`MH` status bits in register 0x0B. A weak or over-strong field causes nonlinearity