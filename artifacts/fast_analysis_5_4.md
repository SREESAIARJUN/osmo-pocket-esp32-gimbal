# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections first

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5047P/AS5048A/B (16384 counts). The derivation below is the same for both; only the resolution numbers change. I give both where it matters.
- **I don't know what "Cycle 5" refers to.** It is not a SimpleFOC release name or function I can identify. It may be a step in your course or project. Below I derive the algorithm as implemented in SimpleFOC's `BLDCMotor::initFOC()` → `alignSensor()` (v2.x). If "Cycle 5" means something specific to you, tell me and I'll map this onto it.

---

## 1. The problem being solved

FOC needs the rotor's **electrical** angle θₑ, meaning the position of the rotor d-axis (magnet flux) relative to the stator α-axis (phase A). The sensor only gives a **mechanical** angle θₘ, measured from wherever the diametric magnet happened to be glued.

For a motor with *p* pole pairs:

$$
\theta_e = \big(p\,\theta_m - \theta_0\big) \bmod 2\pi
$$

There are three unknowns:

| Unknown | Meaning | Found by |
|---|---|---|
| sign(dir) | Does the sensor count up when the field rotates +? | Direction test |
| p | Pole pairs | Given by the user, then sanity-checked |
| θ₀ (`zero_electric_angle`) | Sensor reading, in electrical units, when d-axis ∥ α-axis | Zero-finding |

In SimpleFOC this is literally:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

---

## 2. Why the alignment angle is 3π/2

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During calibration the code uses U_d = 0 and U_q = U_align:

$$
U_\alpha = -U_{align}\sin\theta,\qquad U_\beta = U_{align}\cos\theta
$$

The resulting voltage vector points at θ + π/2. We want the stator field on the **+α axis** (angle 0), so we need θ + π/2 ≡ 0, which gives **θ = 3π/2**.

With no load, the rotor magnet's d-axis aligns with this static stator field. The rotor is then at the electrical angle that defines θₑ = 0. Whatever the sensor reads at that moment is the offset.

---

## 3. Step A: Direction detection

The idea is to rotate the stator field forward by exactly one electrical revolution (open loop), then back, and see which way the sensor moved.

```
for i = 0..500:
    θ = 3π/2 + 2π·i/500
    setPhaseVoltage(U_align, 0, θ); sensor.update(); delay(2ms)
mid_angle = sensor.getAngle()
for i = 500..0:
    (same, backwards)
end_angle = sensor.getAngle()
setPhaseVoltage(0,0,0)

moved = |mid_angle − end_angle|
if moved < MIN_ANGLE_DETECT_MOVEMENT  → fail (motor didn't move: check wiring/U_align)
sensor_direction = (mid_angle < end_angle) ? CCW : CW
```

The reasoning: if the field went +2π electrical and the sensor value increased (mid > end), the directions agree (CW = +1). Otherwise they disagree and the sign is flipped.

## 4. Step B: Pole-pair sanity check

One electrical revolution equals 2π/p mechanical. So:

$$
p \cdot \text{moved} \approx 2\pi
\quad\Rightarrow\quad
\text{check } \big|\,p\cdot\text{moved} - 2\pi\,\big| < 0.5
$$

If this fails, SimpleFOC prints `PP check: fail - estimated pp: 2π/moved`. That usually means:
- the wrong `pole_pairs` value, or
- the rotor slipped because U_align is too low or the load is too high.

## 5. Step C: Zero electric angle

```
setPhaseVoltage(U_align, 0, 3π/2)    // field on +α
delay(700ms)                         // let rotor settle
sensor.update()
zero_electric_angle = 0
zero_electric_angle = electricalAngle()   // = norm(dir·p·θm)
setPhaseVoltage(0,0,0)
```

The temporary `zero_electric_angle = 0` makes `electricalAngle()` return the raw p·θₘ value. Storing that value means that in all later calls, θₑ = 0 exactly at the α-aligned rotor position. That is the definition we want.

---

## 6. AS5600-specific error budget

**Sensor resolution:**
- Mechanical LSB: 2π/4096 ≈ 1.53 mrad (0.088°).
- Electrical LSB: p·2π/4096. For a 7-pole-pair gimbal motor this is about 10.7 mrad (0.62°).

**Offset quantization error** is ±½ LSB electrical, about ±0.31° at p = 7. The torque loss is cos(error), which is negligible (< 0.01%).

**Noise:** The AS5600's raw output jitters by roughly ±1–2 LSB. Averaging N samples while the rotor is held reduces this by √N. This is a cheap improvement over the single sample SimpleFOC takes:

```cpp
float acc = 0;
for (int i = 0; i < 64; i++) { sensor->update(); acc += sensor->getMechanicalAngle(); delay(2); }
// note: the AS5600 wraps at 0/2π – unwrap or average sin/cos if near the seam
float th = atan2(sum_sin, sum_cos);
```

**Larger error sources** (these dominate quantization):
- **Cogging and friction.** The rotor doesn't sit exactly on the field axis. Raise `voltage_sensor_align` until the result stops changing between runs, typically 1–3 V for a gimbal motor and less for low-resistance motors.
- **Magnet eccentricity and air gap.** These cause a periodic, nonlinear error that a single offset cannot fix. Keep the magnet 0.5–3 mm from the chip and check the MD/ML/MH bits in the STATUS register (0x0B).
- **I²C latency.** About 100–200 µs per read at 400 kHz. This adds speed-dependent phase lag, not an offset error. Use the `OUT` pin in analog or PWM mode, or move to an SPI AS5047P, if you need high speed.

---

## 7. Configuration and reuse

```cpp
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);  // addr 0x36, raw angle reg 0x0C, 12 bit
BLDCMotor motor(7);
...
motor.voltage_sensor_align = 2;   // V
motor.linkSensor(&sensor);
motor.init();
motor.initFOC();                  // runs Steps A–C
Serial.println(motor.zero_electric_angle);  // record it
Serial.println(motor.sensor_direction);
```

After the first run, you can skip calibration (as long as the magnet and motor are not re-mounted):

```cpp
motor.zero_electric_angle = 2.15;        // value printed earlier
motor.sensor_direction   = Direction::CW;
motor.initFOC();                         // skips alignment
```

---

## 8. Summary

$$
\boxed{\theta_e = \operatorname{norm}\!\big(\,d\,p\,\theta_m - \theta_0\big),\quad
\theta_0 = \operatorname{norm}\!\big(d\,p\,\theta_m\big)\Big|_{\text{stator field at }3\pi/2\ (\text{+}\alpha)}}
$$

- d comes from a ±1 electrical-revolution sweep.
- p is verified by checking that p·Δθₘ ≈ 2π.
- θ₀ is read with the rotor locked on the α-axis.

For the AS5600 this calibration is limited mainly by mechanics (cogging, magnet placement) rather than its 12-bit resolution.