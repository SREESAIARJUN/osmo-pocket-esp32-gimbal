# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## A correction first

The **AS5600 is a 12-bit encoder**, not 14-bit. It returns 0–4095 counts per revolution, about 0.088° per count. The 14-bit parts you may be thinking of are the AS5048A/B or AS5047P (16384 counts). The algorithm below works the same for any of them; only the resolution constant changes.

I don't recognise "Cycle 17" as a SimpleFOC release, document or course module. The derivation below follows SimpleFOC's actual `BLDCMotor::initFOC()` → `alignSensor()` logic (v2.x). If "Cycle 17" refers to a specific assignment or variant, tell me what it changes.

---

## 1. What needs to be found

FOC needs the rotor's **electrical** angle θₑ, the angle of the rotor flux d-axis relative to the stator α-axis (phase A). The sensor gives a **mechanical** angle θₘ with an arbitrary zero, because the magnet was glued on at some random orientation.

The relationship is:

$$
\theta_e = \big[\, d \cdot p \cdot \theta_m \;-\; \theta_0 \,\big] \bmod 2\pi
$$

where:

| Symbol | Meaning | Values |
|---|---|---|
| p | pole pairs | known, but should be verified |
| d | sensor direction | +1 (CW) or −1 (CCW): does the sensor count up when the field rotates positively? |
| θ₀ | `zero_electric_angle` | the unknown offset |

The calibration therefore has to find **d** and **θ₀**, and sanity-check **p**.

---

## 2. Choosing the alignment vector

SimpleFOC's inverse Park transform inside `setPhaseVoltage(Uq, Ud, θ)` is:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

Calibration uses only Uq = V_align with Ud = 0, at θ = 3π/2:

$$
U_\alpha = -V\sin(3\pi/2) = +V,\qquad U_\beta = V\cos(3\pi/2) = 0
$$

So the stator field points exactly along the **α-axis (phase A)**. With no load, the rotor's d-axis (magnet north) snaps into alignment with this field. By definition, that rotor position is **θₑ = 0**.

This is why the library sweeps and aligns around `_3PI_2`. Applying "q-axis voltage at 3π/2" is equivalent to applying d-axis voltage at 0.

---

## 3. Step A: direction detection and pole-pair check

The procedure:

1. Rotate the field slowly through one electrical revolution forward, then one backward:
   - θ_k = 3π/2 + 2πk/500, for k = 0…500 (forward), then 500…0 (backward).
   - Hold each step for about 2 ms so the rotor follows.
2. Record the mechanical angle at the end of the forward sweep (θ_mid) and the end of the backward sweep (θ_end).
3. One electrical revolution forward should move the rotor by 2π/p mechanically:

$$
\Delta = \theta_{mid} - \theta_{end}
$$

The decisions that follow from Δ:

- **|Δ| < `MIN_ANGLE_DETECT_MOVEMENT`**: the rotor didn't move. Fail. Likely causes are V_align too low, phase wiring, or the sensor.
- **Δ > 0** → d = +1 (CW).
- **Δ < 0** → d = −1 (CCW).
- **Pole-pair check**: if |p·|Δ| − 2π| > 0.5 rad, warn. The pole-pair count is wrong, or the rotor stuck or slipped.
- **Estimated pole pairs**: p ≈ 2π / |Δ|.

Sweeping back and forth also cancels most cogging and friction lag, since the lag is in opposite directions on each pass.

---

## 4. Step B: zero offset

The procedure:

1. Apply the α-axis vector and let the rotor settle:
   `setPhaseVoltage(V_align, 0, _3PI_2);` then `delay(700);`
2. Read the sensor: `sensor->update();` gives θₘ*.
3. At this pose θₑ = 0 by construction, so from the model:

$$
0 = d\,p\,\theta_m^* - \theta_0 \;\Rightarrow\; \theta_0 = \big[d\,p\,\theta_m^*\big] \bmod 2\pi
$$

4. Release the motor: `setPhaseVoltage(0, 0, 0);`

In SimpleFOC this is done by first setting `zero_electric_angle = 0` and then calling `electricalAngle()`, which evaluates the same expression.

---

## 5. Reference implementation

```cpp
int alignSensor(BLDCMotor& m, Sensor& s) {
  const float V = m.voltage_sensor_align;

  // --- A: direction ---
  for (int i = 0; i <= 500; i++) {                 // forward
    m.setPhaseVoltage(V, 0, _3PI_2 + _2PI * i / 500.0f);
    s.update(); _delay(2);
  }
  s.update(); float mid = s.getAngle();
  for (int i = 500; i >= 0; i--) {                 // backward
    m.setPhaseVoltage(V, 0, _3PI_2 + _2PI * i / 500.0f);
    s.update(); _delay(2);
  }
  s.update(); float end = s.getAngle();
  m.setPhaseVoltage(0, 0, 0); _delay(200);

  float moved = fabs(mid - end);
  if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;          // failed
  m.sensor_direction = (mid < end) ? Direction::CCW : Direction::CW;
  if (fabs(moved * m.pole_pairs - _2PI) > 0.5f)
    SIMPLEFOC_DEBUG("PP check failed, est. pp=", _2PI / moved);

  // --- B: zero offset ---
  m.setPhaseVoltage(V, 0, _3PI_2);
  _delay(700);
  s.update();
  m.zero_electric_angle = 0;
  m.zero_electric_angle = m.electricalAngle();   // = norm(dir*pp*θm)
  _delay(20);
  m.setPhaseVoltage(0, 0, 0); _delay(200);
  return 1;
}
```

Once you have the results, skip calibration on later boots by setting them before `initFOC()`:

```cpp
motor.zero_electric_angle = 2.1503;          // value printed at first run
motor.sensor_direction    = Direction::CW;
motor.initFOC();
```

---

## 6. AS5600-specific accuracy considerations

**Quantization.** Each count is 2π/4096 mechanically, which becomes p·0.088° electrically. For a 7-pole-pair gimbal motor that is about 0.62° electrical, giving a torque loss of 1 − cos(0.62°), which is negligible.

**Noise is the bigger issue.** The AS5600 has roughly ±1–2 LSB of jitter, and its built-in filter (the SF bits) adds lag. Improve the single-shot θ₀ by averaging N readings with a circular mean, which handles the 0/2π wrap correctly:

$$
\theta_0 = \operatorname{atan2}\Big(\sum \sin(d p \theta_{m,i}),\;\sum \cos(d p \theta_{m,i})\Big)
$$

**Cogging bias.** Align at several electrical angles θ_k (for example 0, π/2, π, 3π/2). At each pose, compute θ₀,k = d·p·θₘ,k − θ_k, then circular-average all the estimates. This removes detent-torque bias.

**Magnet eccentricity** causes a once-per-mechanical-revolution error that a single θ₀ cannot fix. Repeat the alignment at all p electrical zeros around the shaft. The spread of θ₀ values shows the eccentricity; a lookup table can correct it if needed.

**Hardware settings:**
- **I²C speed**: use 400 kHz or faster. At 100 kHz the read latency adds angle lag at speed.
- **Analog/PWM output mode**: if you use it instead of I²C, its added noise directly degrades θ₀.
- **Magnet gap**: check the MD/ML/MH status bits to confirm the magnet is detected and the field strength is in range. A weak field corrupts the calibration.

**Alignment voltage.** Set V_align so the alignment current is about 20–50% of rated current. Too low and friction causes misalignment; too high and the motor overheats during the 700 ms hold.

---

**Summary:** energize the α-axis (θ = 3π/2 with q-voltage), which defines θₑ = 0. Then a forward/backward sweep determines d and verifies p, and the settled reading gives θ₀ = (d·p·θₘ*) mod 2π.