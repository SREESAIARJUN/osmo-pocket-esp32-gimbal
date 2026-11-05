# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. A correction first

The **AS5600 is a 12-bit sensor** (4096 counts/rev, about 0.088° mechanical), not 14-bit. The 14-bit parts are the AS5047P, AS5048A/B and MT6701. The derivation below holds for any absolute magnetic encoder; only the resolution numbers change. I've used 12 bits throughout and noted where 14 bits would differ.

I don't know what "Cycle 29" refers to. It isn't a SimpleFOC concept, so I've ignored it. Tell me if it's from a specific course or document.

---

## 1. Problem statement

FOC requires the **electrical angle** θₑ of the rotor flux (d-axis) relative to the stator α-axis (phase A). The sensor only gives the **mechanical angle** θₘ, measured from an arbitrary magnet orientation.

For a motor with *p* pole pairs:

$$
\theta_e = \big(s \cdot p \cdot \theta_m - \theta_{0}\big) \bmod 2\pi
$$

| Symbol | Meaning |
|---|---|
| *p* | pole pairs (known or verified) |
| *s* ∈ {+1, −1} | sensor direction: does sensor CW match motor electrical CW? |
| θ₀ | `zero_electric_angle`, the unknown offset between sensor zero and electrical zero |

Calibration has to determine **s** and **θ₀** (and optionally verify *p*).

This matches SimpleFOC's `BLDCMotor::electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

---

## 2. Deriving the alignment angle (why SimpleFOC uses 3π/2)

`setPhaseVoltage(Uq, Ud, angle_el)` performs an inverse Park transform:

$$
u_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
u_\beta = U_d\sin\theta + U_q\cos\theta
$$

With **Ud = 0, Uq = V**:

$$
u_\alpha = -V\sin\theta,\quad u_\beta = V\cos\theta
\;\Rightarrow\; \angle u = \theta + \tfrac{\pi}{2}
$$

The aim is to place the stator field on the α-axis (angle 0), so that the rotor's d-axis snaps to electrical zero:

$$
\theta + \tfrac{\pi}{2} = 2\pi \;\Rightarrow\; \theta = \tfrac{3\pi}{2}
$$

So the alignment call is `setPhaseVoltage(voltage_sensor_align, 0, _3PI_2)`.

With a pure stationary vector the rotor locks to the nearest of *p* equivalent stable positions. Any of them is valid, because each corresponds to θₑ = 0.

---

## 3. Deriving θ₀

At equilibrium the true electrical angle is 0. Let θₘ* be the sensor reading there. Then:

$$
0 = s\,p\,\theta_m^* - \theta_0 \pmod{2\pi}
\;\Rightarrow\;
\boxed{\theta_0 = (s\,p\,\theta_m^*) \bmod 2\pi}
$$

SimpleFOC computes this by temporarily setting `zero_electric_angle = 0` and calling `electricalAngle()`. That returns exactly the boxed expression.

---

## 4. Direction detection (must run before the offset)

The motor is driven open-loop through one electrical revolution, and the sensor change is observed:

1. Ramp θ from 0 → 2π in ~500 steps at `voltage_sensor_align`, then read θₘ,mid.
2. Ramp back 2π → 0, then read θₘ,end.
3. Classify the movement:
   - If Δ = θₘ,mid − θₘ,end < 0: set *s* = −1 (CCW).
   - Otherwise: set *s* = +1 (CW).
   - If |Δ| is below about 0.0007 rad: the motor didn't move, so fail. Causes include no power, too low a voltage, or a wrong driver enable.

**Pole-pair check.** One electrical revolution should rotate the shaft 2π/p mechanically:

$$
\text{if } \big|\,|\Delta|\cdot p - 2\pi\big| > 0.5 \text{ rad} \Rightarrow \text{warn: PP check failed}
$$

From this you can estimate p̂ = round(2π/|Δ|).

---

## 5. Zero-finding on an absolute encoder

The AS5600 is **absolute within one revolution**. That changes two things:

- **No index search is required.** `needsSearch()` returns 0. That function is for incremental ABI encoders, where the controller rotates slowly until the index pulse fires.
- **θ₀ is constant across power cycles** as long as the magnet doesn't slip. You can therefore store θ₀ and *s*, then skip calibration:

```cpp
motor.zero_electric_angle = 2.1563;          // from a previous run
motor.sensor_direction    = Direction::CW;
motor.initFOC();                              // skips alignment
```

---

## 6. The full algorithm (SimpleFOC-style, with improvements)

```cpp
int alignSensorAS5600(BLDCMotor& m, Sensor& s, float Ualign) {
  // --- 1. Direction + pole-pair check ---
  for (int i = 0; i <= 500; i++) {
    m.setPhaseVoltage(Ualign, 0, _3PI_2 + _2PI * i / 500.0f);
    s.update(); _delay(2);
  }
  s.update(); float mid = s.getAngle();
  for (int i = 500; i >= 0; i--) {
    m.setPhaseVoltage(Ualign, 0, _3PI_2 + _2PI * i / 500.0f);
    s.update(); _delay(2);
  }
  s.update(); float end = s.getAngle();
  m.setPhaseVoltage(0, 0, 0); _delay(200);

  float moved = fabs(mid - end);
  if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;          // no motion -> fail
  m.sensor_direction = (mid < end) ? Direction::CCW : Direction::CW;
  if (fabs(moved * m.pole_pairs - _2PI) > 0.5f)
      SIMPLEFOC_DEBUG("PP check failed, estimate:", _2PI / moved);

  // --- 2. Offset: multi-position, bidirectional, circular mean ---
  // Averaging over several electrical-zero positions and both approach
  // directions cancels cogging, friction hysteresis and magnet eccentricity.
  const int N = 8;
  float sx = 0, sy = 0;
  for (int k = 0; k < N; k++) {
    for (int dir = -1; dir <= 1; dir += 2) {
      // approach electrical zero from one side
      for (int j = 0; j <= 50; j++) {
        m.setPhaseVoltage(Ualign, 0, _3PI_2 + dir * 0.5f * (1 - j / 50.0f));
        _delay(3);
      }
      _delay(300);                                    // settle
      float acc = 0;
      for (int r = 0; r < 16; r++) { s.update(); acc += s.getMechanicalAngle(); _delay(1); }
      float th = _normalizeAngle((int)m.sensor_direction * m.pole_pairs * (acc / 16));
      sx += cosf(th); sy += sinf(th);                 // circular mean (handles 0/2pi wrap)
    }
    // advance to the next mechanical zero-position (one electrical rev)
    for (int i = 0; i <= 100; i++) {
      m.setPhaseVoltage(Ualign, 0, _3PI_2 + _2PI * i / 100.0f); _delay(2);
    }
  }
  m.zero_electric_angle = _normalizeAngle(atan2f(sy, sx));
  m.setPhaseVoltage(0, 0, 0);
  return 1;
}
```

> **Why the circular mean?** θ₀ values near 0 and 2π would average to π with a naive mean. Averaging unit vectors avoids that.
>
> **Why not average raw mechanical readings across positions?** The *p* stable positions differ mechanically. Each must be mapped to the electrical angle *before* averaging.

---

## 7. Error budget and practical numbers

**Quantization.** The electrical resolution is:

$$
\delta\theta_e = \frac{360^\circ \cdot p}{2^{n}}
$$

For *p* = 7: 12-bit gives **0.62° el** and 14-bit gives 0.15° el. An offset error ε reduces torque by a factor of cos ε and adds d-axis current proportional to sin ε. At 1° that is a negligible 0.015% torque loss. Even 5° costs only 0.4% torque, but it does produce noticeable direction-dependent asymmetry.

**Dominant real errors (AS5600):**

| Error source | Effect | Mitig