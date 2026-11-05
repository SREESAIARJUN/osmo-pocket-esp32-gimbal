# Electrical Zero-Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections before the derivation

**The AS5600 is 12-bit, not 14-bit.** It reports 4096 counts per revolution, or 0.0879° per count. The 14-bit (16384 count) parts are the AS5047P, AS5048A/B and AS5147. SimpleFOC handles this through the CPR it is given:

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);   // cpr = 4096 (2^12)
// For a 14-bit AS5048A you would instead use:
MagneticSensorI2C sensor = MagneticSensorI2C(AS5048_I2C);   // cpr = 16384
```

The derivation below is written for a general resolution N (bits) and evaluated for N = 12. If your sensor really is 14-bit, substitute N = 14.

**"Cycle 3" is not a SimpleFOC term.** I am treating it as your third iteration of the calibration. The derivation covers the full algorithm as implemented in `FOCMotor::initFOC()` → `BLDCMotor::alignSensor()`, followed by refinements.

---

## 1. What has to be found

FOC needs the rotor's **electrical** angle θₑ, meaning the angle of the rotor d-axis (magnet flux) relative to the stator α-axis (phase A). The encoder only gives a **mechanical** angle θₘ, measured from an arbitrary magnet orientation. The two are related by:

$$
\theta_e = \big(\,d \cdot p \cdot \theta_m \;-\; \theta_0\,\big) \bmod 2\pi
$$

| Symbol | Meaning | SimpleFOC variable |
|---|---|---|
| p | pole pairs | `pole_pairs` |
| d ∈ {+1, −1} | sensor counting direction relative to the motor's electrical rotation | `sensor_direction` |
| θ₀ | electrical zero offset | `zero_electric_angle` |

This is implemented in `BLDCMotor::electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

Calibration therefore means determining **d**, verifying **p**, and measuring **θ₀**.

---

## 2. Step A: Direction detection (and pole-pair check)

### Procedure

Drive the motor open-loop with a slowly rotating voltage vector. Sweep the electrical angle 0 → 2π, which moves the rotor exactly one electrical revolution (1/p of a mechanical turn). Then sweep back.

```cpp
// forward sweep
for (int i = 0; i <= 500; i++) {
  float angle = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, angle);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

// backward sweep
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

### Derivation

The rotor tracks the field, so the forward sweep moves it by Δθₑ = +2π, which is Δθₘ = 2π/p mechanically. The sensor reports some Δθ_sensor = θ_mid − θ_end. Then:

$$
d = \operatorname{sign}(\theta_{mid} - \theta_{end})\cdot(-1)
\;\Rightarrow\;
\begin{cases}
\theta_{mid} < \theta_{end} & \Rightarrow d = \text{CCW} (-1)\\
\theta_{mid} > \theta_{end} & \Rightarrow d = \text{CW} (+1)
\end{cases}
$$

```cpp
float moved = fabs(mid_angle - end_angle);
if (moved < MIN_ANGLE_DETECT_MOVEMENT) { /* fail: rotor did not move */ return 0; }
sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;
```

The `getAngle()` call (full rotations plus angle) is used rather than `getMechanicalAngle()` so that a crossing of the 0/2π wrap does not corrupt the difference.

### Pole-pair check

One electrical revolution should equal 2π/p mechanical:

$$
p \cdot |\Delta\theta_m| \approx 2\pi
\quad\Longrightarrow\quad
\left|\,p\cdot\text{moved} - 2\pi\,\right| < 0.5 \text{ rad}
$$

```cpp
if (fabs(moved * pole_pairs - _2PI) > 0.5f) {
  SIMPLEFOC_DEBUG("MOT: PP check: fail - estimated pp: ", _2PI / moved);
}
```

This is a warning, not a failure. It also gives a free pole-pair estimate: p̂ = 2π / moved. With the AS5600, the quantization error on `moved` is ±1 count ≈ 1.5 mrad, which is negligible against the 0.5 rad tolerance.

---

## 3. Step B: Zero electric angle

### 3.1 Why SimpleFOC uses the angle 3π/2

`setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
\begin{aligned}
U_\alpha &= U_d\cos\theta - U_q\sin\theta \\
U_\beta  &= U_d\sin\theta + U_q\cos\theta
\end{aligned}
$$

The call is `setPhaseVoltage(V_align, 0, 3π/2)`, so Uq = V and Ud = 0:

$$
U_\alpha = -V\sin\tfrac{3\pi}{2} = +V,\qquad
U_\beta = V\cos\tfrac{3\pi}{2} = 0
$$

The stator field therefore points along **+α (phase A axis)**. With no load, the rotor's d-axis (magnet north) settles onto the stator field direction, at the point where the torque, proportional to sin(θ_field − θ_rotor), is zero and stable. By definition this rotor position is **θₑ = 0**.

The reason for "3π/2" rather than 0 is that SimpleFOC passes the voltage as Uq. A q-axis vector leads the reference angle by π/2, so 3π/2 + π/2 = 2π ≡ 0.

### 3.2 Measuring the offset

At the locked position, true θₑ = 0, so from the model in Section 1:

$$
0 = d\,p\,\theta_m^{lock} - \theta_0
\quad\Longrightarrow\quad
\boxed{\theta_0 = (d\,p\,\theta_m^{lock}) \bmod 2\pi}
$$

In code, the offset is computed by calling `electricalAngle()` while `zero_electric_angle` is still 0:

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                       // let rotor settle, oscillation decay
sensor->update();                  // AS5600: one I2C read of RAW ANGLE (0x0C/0x0D)
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();   // = normalize(d*p*theta_m_lock)
_delay(20);
setPhaseVoltage(0, 0, 0);
_delay(200);
```

The 700 ms delay is chosen empirically. The rotor is an underdamped mass on a magnetic spring, and the settling time scales as √(J/k), where k is the torque stiffness at the alignment voltage.

### 3.3 Running with the result

From then on, every `loopFOC()` computes:

$$
\theta_e = d\,p\,\theta_m - \theta_0
$$

and applies `setPhaseVoltage(Uq, Ud, θₑ)`. Because θₑ = 0 now corresponds to the rotor d-axis lying on α, a pure Uq produces a vector 90° ahead of the flux. That is the maximum torque per amp.

---

## 4. Error budget (AS5600, 12-bit)

An offset error δ (electrical) reduces torque by cos δ and injects a d-axis current component proportional to sin δ.

| Source | Mechanical | Electrical (× p, e.g. p = 7) | Torque loss |
|---|---|---|---|
| Quantization, ½ LSB = π/4096 | 0.77 mrad | 5.4 mrad (0.31°) | ≈ 0 |
| AS5600 INL (±1