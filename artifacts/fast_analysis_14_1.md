# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## A correction first

The **AS5600 is a 12-bit encoder** (4096 counts/rev), not 14-bit. The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The math below works for any resolution. I'll use 12 bits for AS5600-specific numbers.

I also don't recognize "Cycle 14." If it refers to a course module, assignment or document revision, please share it and I'll align with it. What follows is the algorithm as implemented in SimpleFOC's `BLDCMotor::initFOC()`, specifically `alignSensor()`.

---

## 1. What needs to be found

FOC needs the rotor's **electrical angle** θₑ, measured from the d-axis relative to stator phase A. The sensor only gives the **mechanical angle** θₘ, measured from an arbitrary magnet mounting position. The relationship is:

$$
\theta_e = \text{norm}_{[0,2\pi)}\big(d \cdot p \cdot \theta_m - \theta_0\big)
$$

| Symbol | Meaning | Found by |
|---|---|---|
| p | pole pairs | user / PP check |
| d ∈ {+1, −1} | sensor direction vs. motor phase sequence | direction test |
| θ₀ | `zero_electric_angle` | zero-finding |

In SimpleFOC this is `electricalAngle()`:
```cpp
_normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle() - zero_electric_angle);
```

## 2. Why the alignment angle is 3π/2

`setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During calibration Ud = 0, so U_α = −U_q sin θ and U_β = U_q cos θ. At θ = 3π/2 this gives:

$$
U_\alpha = U_q,\quad U_\beta = 0
$$

The stator field points straight along the α-axis (phase A). A PM rotor with no load settles with its **d-axis on the stator field**. At that point the true electrical angle is, by definition, θₑ = 0.

Setting θₑ = 0 in the model and solving gives the offset:

$$
0 = d\,p\,\theta_m^{align} - \theta_0 \;\;\Rightarrow\;\; \theta_0 = \text{norm}\big(d\,p\,\theta_m^{align}\big)
$$

So zero-finding reduces to: lock the rotor at the α-axis, read the sensor, and multiply by d·p.

## 3. Step A: direction and pole-pair check

The direction must be known before θ₀, because θ₀ depends on d.

1. **Sweep forward.** Step the open-loop field one electrical revolution starting at 3π/2.
   ```cpp
   for (i = 0; i <= 500; i++) {
     float a = _3PI_2 + _2PI * i / 500.0f;
     setPhaseVoltage(voltage_sensor_align, 0, a);
     sensor->update(); _delay(2);
   }
   ```
   Then record `mid_angle = sensor->getAngle()`.

2. **Sweep back.** Run the same loop in reverse, then record `end_angle`.

3. **Check that the rotor moved.**
   ```cpp
   moved = fabs(mid_angle - end_angle);
   if (moved < MIN_ANGLE_DETECT_MOVEMENT)  -> fail
   ```
   A failure here usually means bad wiring, too little voltage, or no magnet.

4. **Determine direction.**
   ```cpp
   sensor_direction = (mid_angle < end_angle) ? CCW : CW;
   ```
   If the sensor decreased while the field advanced, d = −1.

5. **Check the pole-pair count.** One electrical revolution should move the rotor 2π/p mechanically:
   $$|p\cdot\Delta\theta_m - 2\pi| > 0.5 \Rightarrow \text{PP check failed}$$

## 4. Step B: zero-finding

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2); // field on alpha axis
_delay(700);                                       // let rotor settle
sensor->update();
zero_electric_angle = 0;                           // so electricalAngle() returns raw d*p*theta_m
zero_electric_angle = electricalAngle();           // = norm(d*p*theta_m_align)
setPhaseVoltage(0, 0, 0);
```

Temporarily zeroing the offset makes `electricalAngle()` return exactly d·p·θₘ, which is θ₀ by the derivation in section 2.

## 5. AS5600-specific considerations

**Resolution**
- Mechanical: 2π / 4096 ≈ 0.088°.
- Electrical: p × 0.088°. For a 7-pole-pair gimbal motor this is about 0.62° electrical, which is fine.

**Error sensitivity.** An offset error δ costs torque per unit current by a factor of cos δ, and leaks a fraction sin δ into the d-axis. Even 5° electrical costs only about 0.4% torque, so AS5600 quantization is negligible. In practice, error is dominated by:
- cogging and friction during settling,
- magnet eccentricity, giving nonlinearity of ±1° mechanical, which equals ±p° electrical. This is the real limit.

**Improvement: averaging.** Lock at several electrical angles θₖ = 3π/2 + 2πk/N, compute θ₀,k = norm(d·p·θₘ,k − (θₖ − 3π/2)), and take the circular mean:

$$\theta_0 = \text{atan2}\Big(\sum\sin\theta_{0,k},\sum\cos\theta_{0,k}\Big)$$

This averages out cogging and eccentricity effects.

**I2C latency.** I2C at 400 kHz takes about 100–200 µs per read. Keep the loop fast, and use `sensor->update()` before reading.

**Setup**
```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
sensor.init();
motor.linkSensor(&sensor);
motor.voltage_sensor_align = 3;  // V; enough to overcome cogging, not overheat
motor.init();
motor.initFOC();
```

**Skipping calibration on later boots.** Print `motor.zero_electric_angle` and `motor.sensor_direction` after the first run. Set them before `initFOC()` on later boots, and the alignment is skipped as long as the magnet and phase wiring are unchanged.

## Summary

$$
\boxed{\;d = \text{sign}(\Delta\theta_m \text{ under +field sweep}),\qquad
\theta_0 = \text{norm}\big(d\,p\,\theta_m\big|_{\text{field at }3\pi/2}\big)\;}
$$

The field at 3π/2 under the Uq convention lies on phase A, so the locked rotor sits at θₑ = 0. Its sensor reading therefore defines the offset.