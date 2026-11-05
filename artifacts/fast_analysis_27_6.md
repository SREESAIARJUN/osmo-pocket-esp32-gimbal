# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

Two corrections first:

- **The AS5600 is a 12-bit encoder** (4096 counts/rev, about 0.088°). The 14-bit parts are the AS5047, AS5048 and AS5147. The derivation below works for either, but the resolution numbers differ.
- **I don't know what "Cycle 27" refers to.** It isn't a SimpleFOC release name or a known term. If it is a lesson, a project iteration or a specific code version, tell me and I can tailor this. The algorithm below matches SimpleFOC's `FOCMotor::alignSensor()` / `BLDCMotor::initFOC()` as of the 2.x releases.

---

## 1. What problem are we solving?

FOC needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator's α-axis (phase A). The encoder only gives a **mechanical angle** θₘ measured from wherever the magnet happens to sit.

```
θₑ = normalize( dir · p · θₘ  −  θ₀ )
```

| Symbol | Meaning |
|---|---|
| p | pole pairs |
| dir | sensor direction, ±1 (whether encoder CW matches motor "positive") |
| θ₀ | `zero_electric_angle`, the unknown offset |

Calibration therefore has to find **dir**, check **p**, and measure **θ₀**.

---

## 2. Where does the rotor go when we apply a fixed voltage?

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` uses the inverse Park transform:

```
Uα = Ud·cosθ − Uq·sinθ
Uβ = Ud·sinθ + Uq·cosθ
```

The calibration applies only Uq (Ud = 0) at θ = 3π/2:

```
Uα = −Uq·sin(3π/2) = +Uq
Uβ =  Uq·cos(3π/2) =  0
```

The stator field therefore points exactly along the **α-axis (phase A)**.

With no load, the rotor's permanent-magnet d-axis aligns with the stator field. The rotor thus parks where **θₑ = 0 by definition**.

> **Why 3π/2 instead of passing Ud?** The q-vector leads the angle argument by π/2. Feeding 3π/2 + π/2 = 2π ≡ 0 aligns the field with α while reusing the Uq-only code path. Later versions allow `Ud` alignment, which gives the same result.

---

## 3. Step 1: Direction and pole-pair check

The algorithm sweeps the electrical angle one full electrical turn forward, then back. This drags the rotor 1/p of a mechanical revolution.

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

### Direction

The forward electrical sweep should produce forward mechanical motion:

```
dir = (mid_angle < end_angle) ? CCW(−1) : CW(+1)
```

In words: if the angle decreased on the forward sweep, the encoder counts opposite to the motor.

### Movement sanity check

```
moved = |mid_angle − end_angle|
if moved < MIN_ANGLE_DETECT_MOVEMENT  →  fail
```

A failure here usually means no movement: a driver problem, too low `voltage_sensor_align`, or a bad sensor.

### Pole-pair check

One electrical revolution equals 2π/p mechanical. So:

```
moved · p ≈ 2π
if |moved·p − 2π| > 0.5  →  warn "PP check failed"
```

A good estimate of the true value is p ≈ round(2π / moved).

---

## 4. Step 2: Zero electric angle

1. Park the rotor at θₑ = 0 using the field from §2.
2. Read the encoder.
3. Whatever electrical angle the encoder implies is the offset.

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);  // field on α-axis
_delay(700);                                        // let rotor settle
sensor->update();
zero_electric_angle = 0;                            // so electricalAngle() returns raw
zero_electric_angle = electricalAngle();            // = normalize(dir·p·θₘ)
_delay(20);
setPhaseVoltage(0, 0, 0);
```

### Derivation

At rest, θₑ,true = 0. Setting θ₀ = normalize(dir·p·θₘ,park) makes the runtime formula return

```
θₑ = normalize(dir·p·θₘ,park − θ₀) = 0
```

at that position. Because the mapping is linear in θₘ, it is correct everywhere, modulo encoder nonlinearity.

### Runtime use

```cpp
float BLDCMotor::electricalAngle() {
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

---

## 5. AS5600-specific details

### Sensor configuration

```cpp
// 0x36 addr, 12 bits, RAW ANGLE reg 0x0C, MSB at bit 11
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
// equivalent: MagneticSensorI2C(0x36, 12, 0x0C, 4);
```

### Resolution and error budget

- Mechanical resolution: Δθₘ = 2π / 4096 ≈ 1.53 mrad.
- Electrical quantization: Δθₑ = p · 2π / 4096. For p = 7 this is about 0.0107 rad (0.62°).
- Torque loss from an offset error ε is proportional to cos ε. A 1° error costs about 0.015% of torque, so quantization is negligible.

### Real error sources (larger than quantization)

1. **Magnet eccentricity and air gap** cause sinusoidal nonlinearity, often ±0.5–1° mechanical, which is ±3.5–7° electrical at p = 7.
2. **Cogging and load** stop the rotor from settling exactly at θₑ = 0.
3. **I²C latency.** At 400 kHz a read takes about 100 µs, which causes phase lag at high speed.
4. **Hysteresis and ZPOS settings.** If you program ZPOS/MPOS on the chip, re-run calibration afterward.

### Practical tuning

- Use `motor.voltage_sensor_align` of roughly 1–3 V for gimbal motors. Use less for low-resistance motors, because I = V/R must not overheat the windings.
- Use the 400 kHz I²C clock: `Wire.setClock(400000)`.

---

## 6. Improved zero-finding (optional, more robust)

The stock single-point method is sensitive to friction and cogging. A multi-point averaging variant:

```cpp
float s = 0, c = 0;
const int N = 8;
for (int k = 0; k < N; k++) {
  float target = _2PI * k / N;                       // desired θe
  motor.setPhaseVoltage(Ualign, 0, target + _3PI_2); // field at θe = target
  _delay(500);
  sensor.update();
  float raw = _normalizeAngle(dir * pp * sensor.getMechanicalAngle());
  float off = raw - target;                          // per-point offset estimate
  s += sinf(off);
  c += cosf(off);                                    // circular mean
}
motor.zero_electric_angle = _normalizeAngle(atan2f(s, c));
motor.setPhaseVoltage(0, 0, 0);
```

Why this is better:

- The **circular mean** avoids wrap-around errors near 0/2π.
- Sampling around a full electrical turn averages out first-harmonic magnet eccentricity and cogging bias.
- You can repeat the sweep in both directions and average to cancel friction hysteresis.

---

## 7. Skipping calibration on later boots

After one successful run, print the values:

```cpp
Serial.println(motor.zero_electric_angle, 4);
Serial.println(motor.sensor_direction);
```

Then hard-code them before `initFOC()`:

```cpp
motor.zero_electric_angle = 2.1563f;          // your measured value
motor.sensor