# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## A correction first

The **AS5600 is a 12-bit encoder** (4096 counts/rev, about 0.088° mechanical), not 14-bit. The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The algorithm below is identical for all of them. Only the resolution numbers change.

I also don't recognize "Cycle 11" as a SimpleFOC term or version. The derivation below follows SimpleFOC's `BLDCMotor::initFOC()` → `alignSensor()` as implemented in the 2.x library.

---

## 1. What we need to find

FOC needs the rotor's **electrical** angle θₑ, measured from the phase-A axis. The sensor only gives a **mechanical** angle θₘ, whose zero is wherever the magnet happened to be glued.

The relationship is:

$$
\theta_e = \text{wrap}_{2\pi}\big(\, d \cdot p \cdot \theta_m - \theta_0 \,\big)
$$

| Symbol | Meaning | How it is found |
|---|---|---|
| p | pole pairs | user-supplied, checked by calibration |
| d ∈ {+1, −1} | sensor direction (CW/CCW) | calibration step A |
| θ₀ | `zero_electric_angle` | calibration step B |

In SimpleFOC code:
```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

---

## 2. Physics behind the alignment trick

`setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

With U_d = 0 and U_q = V_align, the stator voltage vector points at angle **θ + π/2**.

The rotor's permanent-magnet flux (its d-axis) snaps into line with the stator field. So if we command θ = **3π/2**:

$$
\theta_{\text{vector}} = \tfrac{3\pi}{2} + \tfrac{\pi}{2} = 2\pi \equiv 0
$$

The stator field then lies on the phase-A axis, and the rotor settles at **θₑ = 0** by definition. This is why SimpleFOC uses `_3PI_2` everywhere in alignment.

---

## 3. Step A: direction detection and pole-pair check

The motor is driven open-loop through exactly one electrical revolution forward, then one backward:

```cpp
// forward one electrical revolution
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

// backward one electrical revolution
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
```

**Direction.** The commanded field rotated by +2π electrical during the forward sweep. If the sensor angle increased, the sensor and the motor agree:

$$
d = \begin{cases} +1\ (\text{CW}) & \theta_{mid} > \theta_{end}\\ -1\ (\text{CCW}) & \theta_{mid} < \theta_{end}\end{cases}
$$

If θ_mid = θ_end, the rotor didn't move. That indicates a failure: V_align is too low, the wiring is wrong, or the sensor is wrong.

**Pole-pair check.** One electrical revolution equals 1/p of a mechanical one. The mechanical travel is:

$$
\Delta\theta_m = |\theta_{mid} - \theta_{end}| \approx \frac{2\pi}{p}
\;\;\Rightarrow\;\; |p\,\Delta\theta_m - 2\pi| < 0.5\ \text{rad} \text{ (pass)}
$$

`getAngle()` is used here rather than `getMechanicalAngle()` because it is multi-turn (rotation-counted). This makes the subtraction immune to the 0/2π wrap.

---

## 4. Step B: zero electric angle

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2); // field on phase-A axis
_delay(700);                                      // let rotor settle
sensor->update();
zero_electric_angle = 0;                          // so electricalAngle() returns raw d·p·θm
zero_electric_angle = electricalAngle();          // = wrap(d·p·θm_align)
_delay(20);
setPhaseVoltage(0, 0, 0);
```

**Derivation.** At rest the true electrical angle is 0, so:

$$
0 = d\,p\,\theta_{m,\text{align}} - \theta_0
\;\;\Rightarrow\;\;
\boxed{\theta_0 = \text{wrap}_{2\pi}\!\big(d\,p\,\theta_{m,\text{align}}\big)}
$$

The code sets `zero_electric_angle = 0` first so that `electricalAngle()` evaluates exactly the right-hand side.

---

## 5. AS5600-specific details

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C); // addr 0x36, RAW ANGLE reg 0x0C, 12 bits
BLDCMotor motor = BLDCMotor(7);                           // e.g. 7 pole pairs
```

- **Mechanical angle:** θₘ = raw · 2π / 4096.
- **Electrical resolution:** p × 0.088°. For p = 7 that is about 0.62° electrical per LSB, which is fine for commutation.
- **ZPOS/MPOS registers:** SimpleFOC reads RAW ANGLE, so these programmed registers are irrelevant. Don't burn them for FOC purposes.
- **I²C latency:** ~100–400 µs per read. Use 400 kHz (`Wire.setClock(400000)`). Read latency at speed adds a velocity-dependent angle lag that calibration cannot remove.
- **Magnet placement:** an off-axis or too-far magnet causes a periodic nonlinearity, one or two cycles per mechanical turn. The single-point θ₀ cannot correct this.

---

## 6. Sensitivity: why accuracy matters

With an offset error δ, current commanded on q actually lands partly on d:

$$
T \propto i_q\cos\delta, \qquad i_d^{\text{parasitic}} = i_q \sin\delta
$$

- **δ = 10°:** about 1.5% torque loss, which is negligible.
- **δ = 30°:** 13% torque loss, plus noticeable heating and asymmetric CW/CCW behaviour.
- **δ ≈ 90°:** no torque, or runaway.

---

## 7. Refinements beyond stock SimpleFOC

1. **Average several readings** during the 700 ms hold. This reduces AS5600 noise of about ±1 LSB.
2. **Bidirectional averaging to cancel friction and cogging lag.** Approach the align position from +ε, then from −ε, and average the two θ₀ values:
   $$\theta_0 = \tfrac12(\theta_0^+ + \theta_0^-)$$
3. **Multi-point calibration.** Align at θ = 3π/2 + k·2π/N for N points per electrical revolution, across all p. Fit θₘ(θₑ), giving a lookup table that corrects magnet eccentricity.
4. **Choose V_align carefully.** Set it to roughly 2–3× the voltage needed to overcome cogging, but keep I = V/R within the motor's rating, since the hold is effectively a stall.

---

## 8. Skipping calibration on later boots

Print the results once, then hard-code them:

```cpp
Serial.println(motor.zero_electric_angle, 4);
Serial.println(motor.sensor_direction);
