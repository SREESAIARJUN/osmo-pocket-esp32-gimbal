# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Two corrections first

1. **The AS5600 is 12-bit, not 14-bit.** It has 4096 counts per revolution, about 0.088° per count. The 14-bit parts are the AS5047P/D, AS5048A/B and MT6701 (16,384 counts). The derivation below is resolution-independent, and I note where the bit depth matters.
2. **"Cycle 19" doesn't match anything I can identify.** It isn't a SimpleFOC release, an AS5600 datasheet section, or a known course module. If it comes from a specific curriculum, share the context and I can adapt this. What follows is the algorithm SimpleFOC actually implements: `FOCMotor::initFOC()` → `alignSensor()` in v2.x.

---

## 1. Sensor model (AS5600 → mechanical angle)

SimpleFOC preset `AS5600_I2C`:

| Parameter | Value |
|---|---|
| I²C address | 0x36 |
| Register | 0x0C/0x0D (RAW ANGLE) |
| Resolution | 12 bits |

The mechanical angle is computed as:

$$\theta_m = \frac{2\pi \cdot \text{raw}}{2^{12}}, \quad \text{raw}\in[0,4095]$$

Multi-turn tracking happens in `Sensor::update()`. If the jump between samples satisfies |Δθ| > 0.8·2π, it is treated as a wrap, and `full_rotations` is incremented or decremented.

## 2. What must be calibrated

FOC needs the rotor's **electrical** angle, which SimpleFOC computes as:

$$\theta_e = \text{normalize}\big(s \cdot p \cdot \theta_m - \theta_{0}\big)$$

where:
- p = pole pairs,
- s ∈ {+1, −1} = `sensor_direction`,
- θ₀ = `zero_electric_angle`.

Two unknowns must be found:
- **s**, because the magnet and chip orientation versus the phase order is arbitrary.
- **θ₀**, because the magnet is glued at an arbitrary angle relative to the rotor flux (d-axis).

## 3. Deriving the alignment angle (why 3π/2)

`setPhaseVoltage(Uq, Ud, θe)` applies the inverse Park transform:

$$U_\alpha = U_d\cos\theta_e - U_q\sin\theta_e,\qquad U_\beta = U_d\sin\theta_e + U_q\cos\theta_e$$

During alignment, U_d = 0 and U_q = V_align. Setting θ_e = 3π/2 gives:

$$U_\alpha = -V\sin(3\pi/2) = +V,\qquad U_\beta = V\cos(3\pi/2) = 0$$

The stator field therefore points along the **α-axis (phase A)**. A locked rotor turns until its d-axis (magnet flux) aligns with the applied field, so the rotor sits at **true electrical angle 0**. Note that the commanded "3π/2" refers to q-axis voltage, which leads d by π/2.

Any sensor reading taken in this state should therefore map to θ_e = 0:

$$0 = s\,p\,\theta_m^{(0)} - \theta_0 \;\;\Rightarrow\;\; \theta_0 = \text{normalize}\big(s\,p\,\theta_m^{(0)}\big)$$

## 4. Algorithm

### Step A: Direction and pole-pair check

```cpp
// sweep one electrical revolution forward
for (int i = 0; i <= 500; i++) {
  float a = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, a);
  sensor->update();
  _delay(2);
}
sensor->update();
float mid_angle = sensor->getAngle();

// sweep back
for (int i = 500; i >= 0; i--) {
  float a = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, a);
  sensor->update();
  _delay(2);
}
sensor->update();
float end_angle = sensor->getAngle();
setPhaseVoltage(0, 0, 0);
_delay(200);

if (mid_angle == end_angle)  -> FAIL (no movement: check wiring/supply/voltage_sensor_align)
sensor_direction = (mid_angle < end_angle) ? Direction::CCW : Direction::CW;

// pole-pair sanity: one electrical rev = 2π/p mechanical
float moved = fabs(mid_angle - end_angle);
if (fabs(moved * pole_pairs - _2PI) > 0.5f) -> warn "PP check failed", estimated pp = _2PI/moved
```

The derivation behind this step:
- One electrical revolution moves the rotor Δθ_m = ±2π/p.
- The sign of Δθ_m gives s.
- The magnitude gives p ≈ 2π/|Δθ_m|.
- Sweeping forward and back cancels most friction/cogging bias in the sign test.

### Step B: Zero-angle capture

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);   // field on α-axis
_delay(700);                                        // let rotor settle
sensor->update();
zero_electric_angle = 0;                            // so electricalAngle() returns s·p·θm
zero_electric_angle = electricalAngle();            // = normalize(s·p·θm)
_delay(20);
setPhaseVoltage(0, 0, 0);
_delay(200);
```

This is exactly θ₀ = normalize(s·p·θ_m⁽⁰⁾) from Section 3.

## 5. Error budget with the AS5600

Electrical angle error is mechanical error multiplied by p:

$$\delta\theta_e = p\cdot\delta\theta_m,\qquad \delta\theta_m^{\text{quant}} = \frac{2\pi}{4096}$$

For a 7-pole-pair gimbal motor, δθ_e ≈ 7 × 0.088° ≈ **0.62°** quantization. A 14-bit sensor would give about 0.15°.

The quantization is negligible in practice. The dominant error sources are:
- **Magnet eccentricity/INL:** typically ±1° mechanical, which becomes about ±7° electrical at p = 7.
- **Settling error from cogging/friction:** keep `voltage_sensor_align` high enough to overcome cogging, but low enough not to overheat the motor.
- **I²C latency:** about 0.1–0.5 ms at 400 kHz. This affects runtime, not calibration, because the rotor is static.

**Optional improvement:** average θ₀ over K samples, or over several aligned positions (θ = 3π/2 + 2πk/N), using circular averaging:

$$\theta_0 = \operatorname{atan2}\Big(\sum\sin\theta_{0,k},\ \sum\cos\theta_{0,k}\Big)$$

This suppresses the first harmonic of magnet eccentricity.

## 6. Skipping calibration on later boots

Print the results once, after the first `initFOC()`:

```cpp
Serial.println(motor.zero_electric_angle, 4);
Serial.println(motor.sensor_direction);   // 1 = CW, -1 = CCW
```

Then hard-code them before `initFOC()` on subsequent boots:

```cpp
motor.zero_electric_angle = 2.1563f;      // your value
motor.sensor_direction    = Direction::CW;
motor.initFOC();                          // alignment skipped
```

Older versions used `motor.initFOC(zero, Direction::CW)` instead.

## 7. Quick validation

1. Run voltage-mode torque control with a small `target` (U_q > 0).
2. The motor should spin smoothly in the positive direction, with equal speed in both directions for ±U_q.
3. If it spins faster one way, offset θ₀ by small amounts. If it vibrates or locks, check:
   - **pole_pairs:** a wrong value gives cyclic torque ripple.
   - **sensor_direction:** if wrong, the motor runs away or locks.
   - **AS5600 magnet distance:** it should be 0.5–3 mm. You can check the MD/ML/MH status bits in register 0x0B.