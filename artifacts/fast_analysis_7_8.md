# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Clarifications

- **The AS5600 is a 12-bit encoder (4096 counts/rev), not 14-bit.** The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The derivation below is the same for any absolute magnetic encoder. Only the count constant changes: N = 4096 for 12-bit, N = 16384 for 14-bit.
- **I don't recognize "Cycle 7"** as a SimpleFOC term or release. If it refers to a course, issue thread or document series, share the context and I'll adapt the answer. What follows matches the SimpleFOC v2.x `alignSensor()` logic.

---

## 1. The quantity we need

FOC needs the rotor's **electrical** angle θₑ, measured from the stator α-axis (phase A) to the rotor d-axis (magnet flux).

The sensor provides only the **mechanical** angle θₘ, measured from the arbitrary zero of the magnet as glued on the shaft:

```
θₘ = 2π · raw / N            raw ∈ [0, 4095], N = 4096
```

The two are related by the pole-pair count p, a direction sign s = ±1, and an unknown constant offset θ₀:

```
θₑ = wrap₂π( s · p · θₘ − θ₀ )                (1)
```

Calibration means finding **s**, verifying **p**, and measuring **θ₀** (`zero_electric_angle`).

Because the AS5600 is absolute, θ₀ is constant across power cycles. There is no index pulse to search for, so `needsSearch()` returns 0. The "zero-finding" step is just measuring θ₀.

---

## 2. Deriving θ₀ — why SimpleFOC commands 3π/2

SimpleFOC applies voltage with `setPhaseVoltage(Uq, Ud, θ_cmd)`. The inverse Park transform is:

```
Uα = Ud·cos θ_cmd − Uq·sin θ_cmd
Uβ = Ud·sin θ_cmd + Uq·cos θ_cmd
```

During alignment, Ud = 0 and Uq = V_align, so:

```
Uα = −V·sin θ_cmd ,   Uβ = V·cos θ_cmd
```

The stator field therefore points at angle θ_cmd + π/2.

We want that field on the α-axis, at angle 0 mod 2π:

```
θ_cmd + π/2 = 2π   ⇒   θ_cmd = 3π/2   (_3PI_2)
```

A stationary stator field pulls the rotor d-axis (north pole) into alignment with it. With θ_cmd = 3π/2, the rotor settles where its true electrical angle is 0.

Substituting θₑ = 0 into (1) at the settled mechanical reading θₘ*:

```
0 = s·p·θₘ* − θ₀   ⇒   θ₀ = wrap₂π( s · p · θₘ* )        (2)
```

That is the whole derivation. In code, set `zero_electric_angle = 0` temporarily, call `electricalAngle()`, and store the result.

---

## 3. Direction and pole-pair check

Before measuring θ₀, SimpleFOC determines the direction sign s and sanity-checks p:

1. Sweep θ_cmd from 0 to 2π in 500 steps (open loop, `voltage_sensor_align`). The rotor follows one electrical revolution, which is 1/p of a mechanical turn. Record `mid_angle`.
2. Sweep back from 2π to 0. Record `end_angle`.
3. Determine the sign:
   - `mid_angle > end_angle` gives s = CCW (−1).
   - Otherwise s = CW (+1).
4. Check the pole pairs. The mechanical movement should be Δθₘ = |mid − end| ≈ 2π/p.
   - If |p·Δθₘ − 2π| > 0.5 rad, warn "PP check failed".
   - The implied pole-pair estimate is p̂ = 2π / Δθₘ.

---

## 4. Algorithm (pseudocode mirroring SimpleFOC)

```cpp
int BLDCMotor::alignSensor() {
  // --- direction / pole pair check ---
  if (sensor_direction == Direction::UNKNOWN) {
    for (int i = 0; i <= 500; i++) {
      float a = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, a);
      sensor->update(); _delay(2);
    }
    sensor->update();
    float mid_angle = sensor->getAngle();

    for (int i = 500; i >= 0; i--) {
      float a = _3PI_2 + _2PI * i / 500.0f;
      setPhaseVoltage(voltage_sensor_align, 0, a);
      sensor->update(); _delay(2);
    }
    sensor->update();
    float end_angle = sensor->getAngle();
    setPhaseVoltage(0, 0, 0); _delay(200);

    float moved = fabs(mid_angle - end_angle);
    if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;   // sensor not moving
    sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;
    if (fabs(moved * pole_pairs - _2PI) > 0.5f)
      SIMPLEFOC_DEBUG("PP check failed, estimate:", _2PI / moved);
  }

  // --- zero electric angle ---
  if (!_isset(zero_electric_angle)) {
    setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);   // field on α-axis
    _delay(700);                                        // let rotor settle
    sensor->update();
    zero_electric_angle = 0;
    zero_electric_angle = electricalAngle();            // eq. (2)
    setPhaseVoltage(0, 0, 0); _delay(200);
  }
  return 1;
}

float FOCMotor::electricalAngle() {                     // eq. (1)
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

To skip calibration on later boots, save the printed values and pass them in:

```cpp
motor.initFOC(zero_electric_angle, Direction::CW);
```

---

## 5. AS5600-specific details

**Register choice**
- Read **RAW ANGLE** (0x0C/0x0D, 12-bit) at I²C address 0x36.
- Avoid **ANGLE** (0x0E/0x0F). It is rescaled by ZPOS/MPOS/MANG, which would make p·θₘ non-linear if those registers are programmed.
- Use `MagneticSensorI2C sensor(AS5600_I2C)`. Its config is 12 bits, register 0x0C, MSB mask/shift.

**Electrical resolution**
```
Δθₑ = 2π·p / 4096
```
For p = 7, this is about 0.0107 rad (0.62° electrical). For p = 14, it is about 1.23°. That is acceptable, but noticeably coarser than a 14-bit sensor (4× finer).

**Turn tracking**
`Sensor::update()` adds ±2π whenever |Δθₘ| > 0.8·2π between samples. At 400 kHz I²C, a read takes about 100–150 µs. That limits safe speed to well under 0.8 rev per sample period, which is not an issue in practice.

**Magnet quality**
Check the STATUS register (0x0B):
- MD = 1 means the magnet is detected.
- MH or ML indicates the field is too strong or too weak.

Weak fields add noise that directly corrupts θ₀.

---

## 6. Error sensitivity of θ₀

Suppose the offset estimate is off by ε. The current you command on q is then split between the axes:

```
I_q,true = I·cos ε    →  torque loss ≈ ε²/2
I_d,true = I·sin ε    →  wasted / field-weakening current
```

The direction of the error matters:
- Positive ε (lead) partially weakens the field.
- Negative ε (lag) partially strengthens it.

Torque differs between directions of rotation, which is a classic symptom of a bad offset.

| ε (electrical) | Torque loss | d-axis current fraction |
|---|---|---|
| 5° | 0.4 % | 8.7 %