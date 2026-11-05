# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

**Two clarifications first:**
- The **AS5600 is a 12-bit encoder** (4096 counts/rev, about 0.088°). The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The derivation below works for any resolution. I use 12 bits for the AS5600 numbers and note where 14 bits would change them.
- I don't know what "Cycle 13" refers to (a course module, lab sheet, or internal sprint). If it has specific requirements, share them and I'll adapt this.

---

## 1. The problem

FOC needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator α-axis (phase A).

The encoder gives a **mechanical angle** θₘ with an arbitrary zero, because the magnet is glued on at a random orientation. So:

$$
\theta_e = \text{norm}\big(d \cdot p \cdot \theta_m - \theta_{0}\big)
$$

| Symbol | Meaning |
|---|---|
| p | Pole pairs |
| d ∈ {+1, −1} | Sensor direction (CW/CCW relative to phase sequence) |
| θ₀ | `zero_electric_angle`, the unknown offset |
| norm(·) | Wrap into [0, 2π) |

Calibration must determine **d**, verify **p**, and find **θ₀**.

---

## 2. Forcing the rotor to a known electrical angle

SimpleFOC's `setPhaseVoltage(Uq, Ud, θ)` applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

During alignment it calls `setPhaseVoltage(U_align, 0, 3π/2)`:

$$
U_\alpha = -U\sin(3\pi/2) = U,\qquad U_\beta = U\cos(3\pi/2) = 0
$$

The stator field therefore points exactly along the **α-axis** (phase A). An unloaded rotor's d-axis (its magnet) snaps to align with that field.

At this equilibrium the true electrical angle is, by definition, **θₑ = 0**.

Why 3π/2 instead of applying Ud directly? The library only expresses voltage as "Uq at angle θ". Since Uq leads d by π/2, putting Uq at 3π/2 puts the field at 3π/2 + π/2 = 2π ≡ 0.

---

## 3. Step A: Direction detection and pole-pair check

The algorithm sweeps the electrical angle forward one full electrical turn, then back. The sweep starts from 3π/2 so it begins at θₑ = 0.

```
for i in 0..500:  θ = 3π/2 + 2π·i/500;  setPhaseVoltage(U,0,θ); delay(2ms)
mid = sensor.getAngle()
for i in 500..0:  same, backwards
end = sensor.getAngle()
```

**Direction:**
$$
d = \begin{cases} -1 & \text{if } mid < end \\ +1 & \text{otherwise} \end{cases}
$$

The forward electrical sweep should increase the sensor angle. If it decreased, the sensor counts opposite to the phase sequence.

**Pole-pair sanity check.** One electrical revolution equals 1/p of a mechanical revolution:

$$
\Delta\theta_m = |mid - end| \approx \frac{2\pi}{p}
\quad\Rightarrow\quad
\text{check } |\Delta\theta_m \cdot p - 2\pi| < 0.5
$$

If the check fails, estimate p̂ = round(2π / Δθₘ). The usual causes are a wrong `pole_pairs` value or a rotor that slips or sticks.

---

## 4. Step B: Zero electrical angle

```
setPhaseVoltage(U_align, 0, 3π/2);   // field on α-axis → θe,true = 0
delay(700);                          // let rotor settle
sensor.update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();   // = norm(d·p·θm)
setPhaseVoltage(0, 0, 0);
```

**Derivation.** At equilibrium the true θₑ is 0, so:

$$
0 = \text{norm}(d\,p\,\theta_m^{*} - \theta_0)\;\Rightarrow\;
\theta_0 = \text{norm}(d\,p\,\theta_m^{*})
$$

That is exactly what `electricalAngle()` returns when temporarily evaluated with θ₀ = 0. After this, every runtime call gives θₑ referenced to phase A.

---

## 5. AS5600 specifics

**Reading the sensor:**
- I²C address `0x36`.
- Read `RAW ANGLE` from registers `0x0C`–`0x0D` (12 bits). Use RAW ANGLE, not the filtered `ANGLE` register, because the hysteresis and ZPOS settings of `ANGLE` would interfere.
- In SimpleFOC: `MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);`

**Conversion:**
$$
\theta_m = \text{raw}\cdot\frac{2\pi}{4096}
$$

**Multi-turn tracking** (`Sensor::update()`):
- If Δθ between samples exceeds 0.8·2π, decrement `full_rotations`.
- If it is below −0.8·2π, increment `full_rotations`.
- This is why update rate matters: at a 1 kHz I²C loop, about 48 000 rpm is the theoretical limit before wrap ambiguity.

**Offset resolution.** One count corresponds to this electrical angle:

$$
\delta\theta_e = p\cdot\frac{2\pi}{4096}
$$

For a 7-pole-pair gimbal motor, that is about 0.62° electrical. With a 14-bit sensor it would be about 0.15°.

**Torque loss from offset error.** An offset error ε reduces torque by a factor of cos ε and injects an Id component of sin ε. Even 5° costs less than 0.4% torque, so quantization is negligible. The real error sources are mechanical.

---

## 6. Practical error sources and an improved algorithm

The stock single-point alignment is biased by:
- **Friction and cogging.** The rotor stops short of the true field direction. The error has opposite sign depending on approach direction.
- **Magnet eccentricity / off-axis mounting.** This causes a sinusoidal error per mechanical revolution, which is common with the AS5600's diametric magnets.
- **Insufficient `voltage_sensor_align`.** Too little voltage leaves the rotor stuck; too much overheats a low-resistance motor. A typical value is 1–3 V for gimbal motors.

**Improved procedure.** Measure at every electrical pole position from both directions, then take a circular mean:

```cpp
float s = 0, c = 0;
for (int k = 0; k < pp; k++) {
  for (int dir = -1; dir <= 1; dir += 2) {
    // approach target from one side
    float tgt = _3PI_2 + k * _2PI;
    for (int i = 0; i <= 50; i++) {
      motor.setPhaseVoltage(U, 0, tgt - dir * 0.5f + dir * 0.5f * i / 50);
      delay(2);
    }
    delay(300);
    sensor.update();
    float e = _normalizeAngle(d * pp * sensor.getMechanicalAngle());
    s += sinf(e);
    c += cosf(e);
  }
}
motor.zero_electric_angle = _normalizeAngle(atan2f(s, c));
motor.setPhaseVoltage(0, 0, 0);
```

How this helps:
- Approaching from both sides cancels the friction/cogging bias.
- Averaging over all p pole positions cancels the once-per-revolution eccentricity error to first order.
- A circular mean (atan2 of summed sin and cos) is required because the readings wrap at 2π. An arithmetic mean would break near the wrap point.

---

## 7. Skipping calibration on subsequent boots

Once calibrated, store θ₀ and d, then reuse them:

```cpp
motor.zero_electric_angle = 2.15f;      // from previous run
motor.sensor_direction   = Direction::CW;
motor.initFOC();                         // skips alignment
```

This only stays valid if the magnet doesn't move relative to the shaft. Re-run calibration after any mechanical disassembly.