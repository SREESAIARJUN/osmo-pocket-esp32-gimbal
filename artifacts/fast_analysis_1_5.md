# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Correction: the AS5600 is 12-bit

The AS5600 is a **12-bit** encoder (4096 counts/rev, about 0.088°/count), not 14-bit. The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The derivation below works for any resolution. The quantization numbers use N = 4096; for a 14-bit part substitute N = 16384.

SimpleFOC reads the AS5600 over I²C through `MagneticSensorI2C` with `AS5600_I2C` config. That reads the raw angle register 0x0C/0x0D with a 12-bit mask.

---

## 1. The problem

FOC needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator α-axis (phase A). The sensor only gives a **mechanical angle** θₘ, measured from wherever the magnet happens to be glued.

For a motor with *p* pole pairs:

$$
\theta_e = \big[\, s \cdot p \cdot \theta_m - \theta_0 \,\big] \bmod 2\pi
$$

| Symbol | Meaning |
|---|---|
| s ∈ {+1, −1} | Sensor direction: does the sensor count up when the field rotates "forward"? |
| p | Pole pairs (known, or verified during calibration) |
| θ₀ | Electrical zero offset (`zero_electric_angle`). This is the unknown. |

Calibration must find **s** and **θ₀**, and can optionally verify **p**.

---

## 2. Deriving the zero offset

### 2.1 Forcing the rotor to a known electrical angle

SimpleFOC's `setPhaseVoltage(Uq, Ud, θₑ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta_e - U_q\sin\theta_e,\qquad
U_\beta = U_d\sin\theta_e + U_q\cos\theta_e
$$

During alignment SimpleFOC calls `setPhaseVoltage(V_align, 0, 3π/2)`:

$$
U_\alpha = -V\sin(3\pi/2) = +V,\qquad U_\beta = V\cos(3\pi/2) = 0
$$

The resulting stator voltage vector, and in steady state the current vector, points along **+α**, which is the phase-A axis. A permanent-magnet rotor with no load settles with its d-axis (magnet flux) aligned to the stator field. Therefore, at equilibrium:

$$
\theta_e^{\text{true}} = 0
$$

Applying Uq at 3π/2 is the same as applying Ud at 0. SimpleFOC phrases it this way so the same q-axis voltage variable can be reused.

### 2.2 Solving for θ₀

At equilibrium, read the sensor to get θₘ\*. Substituting into the model:

$$
0 = s\,p\,\theta_m^* - \theta_0 \;\;\Rightarrow\;\; \boxed{\theta_0 = \big[s\,p\,\theta_m^*\big] \bmod 2\pi}
$$

This is exactly what SimpleFOC does:

```cpp
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();  // = normalize(s * p * θm*)
```

### 2.3 Using it at runtime

```cpp
float electricalAngle() {
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

---

## 3. Deriving the direction (and checking the pole pairs)

Sweep the commanded electrical angle forward and watch how the sensor moves.

**Procedure:**

1. Ramp θₑ,cmd from 0 to 2π in small steps, then back down to 0.
2. Record θₘ at the start (θₘ,mid after the forward sweep) and at the end.
3. Compute the mechanical change Δθₘ = θₘ,end − θₘ,start, using the multi-turn angle so wrap-around is handled.

**Why it works:** forward rotation by 2π electrical moves the rotor by 2π/p mechanical. So:

$$
s = \operatorname{sign}(\Delta\theta_m)
$$

If |Δθₘ| ≈ 0, the motor did not move. Likely causes are too little voltage, a disconnected phase, or the magnet missing.

**Pole-pair check:**

$$
\hat p = \frac{2\pi}{|\Delta\theta_m|}
$$

If |p̂ − p| > 0.5, warn the user. SimpleFOC prints "PP check: fail" in this case.

Do the direction sweep **before** the zero measurement, because θ₀ depends on s.

---

## 4. Error budget for the AS5600

**Quantization.** One sensor count produces an electrical error of:

$$
\delta\theta_e = p\cdot\frac{2\pi}{4096}
$$

| p | δθₑ per count | Torque loss, 1 − cos(δθₑ) |
|---|---|---|
| 7 | 0.62° | ≈ 0.006 % |
| 11 | 0.97° | ≈ 0.014 % |
| 14 | 1.23° | ≈ 0.023 % |

Quantization is negligible.

**Real error sources, which dominate:**

- **Load and cogging torque.** The rotor stops at an angle φ where alignment torque balances load torque: K·sin φ = T_load. The fix is a higher `voltage_sensor_align`, an unloaded motor, and a settle delay of about 700 ms or more.
- **Sensor noise.** The AS5600 typically shows ±1–2 counts of jitter. Average M samples; for angles, use the circular mean.
- **Magnet eccentricity and nonlinearity.** The AS5600 is affected by magnet placement. A single-point θ₀ is only correct at one rotor position, and the error repeats once per mechanical revolution. This is handled by the refinement in Section 6.
- **I²C latency.** A 400 kHz read takes about 100 µs+. This does not matter while the rotor is stationary, but it matters at speed (Δθₑ = p·ω·t_lat). Use `Wire.setClock(400000)`.

---

## 5. Implementation

```cpp
#include <SimpleFOC.h>

MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);   // 12-bit, 0x36
BLDCMotor motor(7);                                          // p = 7
BLDCDriver3PWM driver(9, 5, 6, 8);

// Circular mean of M angle samples (avoids wrap-around bias)
float sampleAngle(int M) {
  float sx = 0, sy = 0;
  for (int i = 0; i < M; i++) {
    sensor.update();
    float a = sensor.getMechanicalAngle();
    sx += _cos(a); sy += _sin(a);
    _delay(2);
  }
  return _normalizeAngle(atan2(sy, sx));
}

int calibrateDirection(float V) {
  // Forward sweep: 0 -> 2π electrical
  for (int i = 0; i <= 500; i++) {
    float a = _3PI_2 + _2PI * i / 500.0f;
    motor.setPhaseVoltage(V, 0, a);
    sensor.update(); _delay(2);
  }
  sensor.update();
  float mid = sensor.getAngle();               // multi-turn angle
  // Backward sweep: 2π -> 0 (removes backlash/hysteresis from the end point)
  for (int i = 500; i >= 0; i--) {
    float a = _3PI_2 + _2PI * i / 500.0f;
    motor.setPhaseVoltage(V, 0, a);
    sensor.update(); _delay(2);
  }
  sensor.update();
  float end = sensor.getAngle();
  motor.setPhaseVoltage(0, 0, 0); _delay(200);

  float d = mid - end;                         // movement for +2π electrical
  if (fabs(d) < 0.01f) { Serial.println("No movement!"); return 0; }
  float pp_est = _2PI / fabs(d);
  Serial.print("PP estimate: "); Serial.println(pp_est);
  return (d > 0) ? +1 : -1;
}

float calibrateZero(float V, int dir) {
  motor.setPhaseVoltage(V, 0, _3PI_2);         // rotor d-axis -> α-axis (θe = 0)
  _delay(700);                                 // settle
  float thm = sampleAngle(32);
  motor.setPhaseVoltage(0, 0, 0);
  return _normalizeAngle(dir * motor.pole_pairs * thm);   // θ0 = s·p·θm*
}
```

Then pass the results to SimpleFOC so it skips its own alignment on the next boot:

```cpp
motor.initFOC();   // with the values below, alignment is skipped
// or: motor.zero_electric_angle = theta0;
//     motor.sensor_direction = (dir > 0) ? Direction::CW : Direction::CCW;
```

---

## 6. Recommended