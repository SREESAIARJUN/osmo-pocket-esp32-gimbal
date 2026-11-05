# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections first

- **The AS5600 is a 12-bit encoder, not 14-bit.** It gives 4096 counts per revolution, about 0.088° per count. The 14-bit parts are the AS5047P and AS5048A/B (16384 counts per revolution). The derivation below works for either; only the counts-per-revolution value changes.
- **I don't recognise "Cycle 21."** It isn't a SimpleFOC release, a function name or a known algorithm stage. If it refers to a course or lab numbering, the content below is the actual SimpleFOC algorithm in `BLDCMotor::initFOC()` → `alignSensor()`.

---

## 1. Sensor model (AS5600 over I²C)

```cpp
// I2C address 0x36, RAW ANGLE register 0x0C (MSB) / 0x0D (LSB), 12 bits
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
// equivalent to: MagneticSensorI2C(0x36, 12, 0x0C, 4);
```

The mechanical angle is the raw count scaled to radians:

$$
\theta_m = \frac{2\pi \cdot \text{raw}}{4096}, \qquad \text{raw} \in [0, 4095]
$$

SimpleFOC tracks full rotations separately, so `getAngle()` is multi-turn. `getMechanicalAngle()` stays in [0, 2π).

## 2. What must be found

FOC needs the rotor's **electrical** angle θₑ, measured in the same αβ frame that the inverse Park/Clarke transform uses. The sensor measures something different:

$$
\theta_e = \text{wrap}_{2\pi}\big(d \cdot p \cdot \theta_m - \theta_0\big)
$$

where:

- **p** is the number of pole pairs. The user supplies it, and the calibration checks it.
- **d ∈ {+1, −1}** is the sensor direction. The magnet may count opposite to the stator's phase sequence.
- **θ₀** is the `zero_electric_angle`, set by the arbitrary mounting angle of the magnet relative to the rotor flux.

This is exactly SimpleFOC's `electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

Calibration therefore has to determine **d**, verify **p**, and measure **θ₀**.

## 3. Key derivation: why SimpleFOC aligns at 3π/2

`setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During alignment, Ud = 0 and Uq = V_align. At θ = 3π/2, where sin = −1 and cos = 0:

$$
U_\alpha = +V_{align}, \qquad U_\beta = 0
$$

So the stator flux vector points exactly along the **+α axis**, which is the phase-A axis. The rotor's d-axis (magnet flux) is pulled into alignment with it.

In the αβ frame, the rotor's d-axis on +α means the true electrical angle is θₑ = 0. Setting θₑ = 0 in the model at the measured θ_m* gives:

$$
0 = d\,p\,\theta_m^* - \theta_0 \;\;\Rightarrow\;\; \boxed{\theta_0 = \text{wrap}_{2\pi}(d\,p\,\theta_m^*)}
$$

This is why the code sets `zero_electric_angle = 0` and then calls `electricalAngle()`. With θ₀ = 0, that call returns exactly d·p·θ_m*.

The "+π/2 phase-shift" intuition says the same thing another way. Commanding Uq at angle θ places the voltage vector at θ + π/2. That is perpendicular to the d-axis, which is where torque is produced. 3π/2 + π/2 = 2π ≡ 0.

## 4. Direction and pole-pair detection

The procedure:

1. Sweep the commanded angle one full electrical revolution forward. Record the sensor angle θ_mid.
2. Sweep the same revolution backward. Record θ_end.
3. One electrical revolution equals 2π/p mechanical:

$$
|\theta_{mid} - \theta_{end}| \approx \frac{2\pi}{p}
$$

From this:

- **Direction:** if θ_mid > θ_end, the sensor counts the same way as the phase sequence (d = CW = +1). Otherwise d = CCW = −1. If they are equal, the motor didn't move: check wiring, `voltage_sensor_align` and the supply.
- **Pole-pair check:** p̂ = 2π / |Δθ|. A warning is raised if |Δθ·p − 2π| > 0.5.

## 5. Reference implementation (mirrors SimpleFOC `alignSensor()`)

```cpp
int BLDCMotor::alignSensor() {
  // ---- (a) direction + pole-pair check ----
  if (sensor_direction == Direction::UNKNOWN) {
    for (int i = 0; i <= 500; i++) {                 // forward 1 elec. rev
      float a = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, a);
      sensor->update(); _delay(2);
    }
    sensor->update(); float mid = sensor->getAngle();
    for (int i = 500; i >= 0; i--) {                 // backward 1 elec. rev
      float a = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, a);
      sensor->update(); _delay(2);
    }
    sensor->update(); float end = sensor->getAngle();
    setPhaseVoltage(0, 0, 0); _delay(200);

    if (mid == end) return 0;                        // no movement -> fail
    sensor_direction = (mid < end) ? Direction::CCW : Direction::CW;

    float moved = fabs(mid - end);
    if (fabs(moved * pole_pairs - _2PI) > 0.5f)
      SIMPLEFOC_DEBUG("MOT: PP check fail, est. pp =", _2PI / moved);
  }

  // ---- (b) zero electrical angle ----
  if (!_isset(zero_electric_angle)) {
    setPhaseVoltage(voltage_sensor_align, 0, _3PI_2); // field on +alpha
    _delay(700);                                      // let rotor settle
    sensor->update();
    zero_electric_angle = 0;
    zero_electric_angle = electricalAngle();          // = wrap(d*p*theta_m*)
    _delay(20);
    setPhaseVoltage(0, 0, 0); _delay(200);
  }
  return 1;
}
```

After the first run, print `motor.zero_electric_angle` and `motor.sensor_direction`. Hard-code them before `initFOC()` to skip calibration on later boots:

```cpp
motor.zero_electric_angle = 2.1503f;   // example value
motor.sensor_direction = Direction::CW;
