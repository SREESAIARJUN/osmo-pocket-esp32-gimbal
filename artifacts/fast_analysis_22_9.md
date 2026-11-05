# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## Two corrections first

1. **The AS5600 is a 12-bit encoder, not 14-bit.** It gives 4096 counts per revolution, about 0.088° per count, and reads over I²C or analog/PWM. The 14-bit parts are the AS5047P, AS5048A/B and MT6701. They use SPI or SSI and give 16384 counts. The math below is the same for either. Only the resolution and latency numbers change, and I note where.
2. **I can't identify "Cycle 22".** It may be a course, sprint or curriculum label. The derivation below follows SimpleFOC's actual `BLDCMotor::alignSensor()` logic (v2.x). If "Cycle 22" refers to a specific variant, the structure should still map onto it.

---

## 1. The quantity being calibrated

SimpleFOC computes the electrical angle as:

$$
\theta_e = \operatorname{wrap}_{2\pi}\Big(d \cdot p \cdot \theta_m - \theta_0\Big)
$$

| Symbol | Meaning |
|---|---|
| $\theta_m$ | Mechanical angle from the sensor (`sensor->getMechanicalAngle()`) |
| $p$ | Pole pairs |
| $d \in \{+1,-1\}$ | `sensor_direction` (CW/CCW) |
| $\theta_0$ | `zero_electric_angle`, the unknown offset |

In code:
```cpp
float FOCMotor::electricalAngle() {
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

Calibration must find **$d$** first, check **$p$**, and then find **$\theta_0$**. Together these make $\theta_e = 0$ exactly when the rotor d-axis (the magnet flux) is aligned with the stator α-axis (phase A).

---

## 2. Forcing the rotor to a known electrical angle

The motor is driven open-loop with `setPhaseVoltage(Uq, Ud, angle_el)`. That function applies the inverse Park transform:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

Alignment calls `setPhaseVoltage(V_align, 0, 3π/2)`. Substituting $U_q = V$, $U_d = 0$ and $\theta = 3\pi/2$:

$$
U_\alpha = -V\sin(3\pi/2) = +V,\qquad U_\beta = V\cos(3\pi/2) = 0
$$

The stator field therefore points purely along +α. A permanent-magnet rotor settles where its flux aligns with the stator field, i.e. at the minimum-energy, zero-torque position. That puts the rotor d-axis on α, so the **true electrical angle is 0**.

This is why the code uses the odd-looking $3\pi/2$. With the q-axis convention, the stator vector lands at electrical zero.

---

## 3. Step A: Direction detection and pole-pair check

The firmware sweeps the field slowly through one electrical revolution forward, then back:

```cpp
// forward sweep: 1 electrical revolution
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

**Direction.** Positive electrical rotation should give positive sensor rotation if the sensor and windings agree:

$$
d = \begin{cases} +1 \ (\text{CW}) & \text{if } \theta_{mid} > \theta_{end} \\ -1 \ (\text{CCW}) & \text{if } \theta_{mid} < \theta_{end} \end{cases}
$$

If $\theta_{mid} = \theta_{end}$, the rotor did not move. That means a failure: the voltage is too low, a phase is open, or the magnet is missing.

**Pole-pair check.** One electrical revolution equals $2\pi/p$ mechanical, so:

$$
\Delta\theta_m = |\theta_{mid} - \theta_{end}| \approx \frac{2\pi}{p}
\;\Rightarrow\;
\big|\,p\,\Delta\theta_m - 2\pi\,\big| < 0.5 \ \text{rad}
$$

```cpp
float moved = fabs(mid_angle - end_angle);
if (fabs(moved * pole_pairs - _2PI) > 0.5f)
  SIMPLEFOC_DEBUG("MOT: PP check: fail - estimated pp: ", _2PI / moved);
```

The tolerance covers cogging, friction lag and sensor nonlinearity.

---

## 4. Step B: Finding the zero offset

Lock the rotor at true electrical zero (section 2), let it settle, then read the sensor:

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                       // settle mechanical oscillation
sensor->update();
zero_electric_angle = 0;           // so electricalAngle() returns the raw product
zero_electric_angle = electricalAngle();
setPhaseVoltage(0, 0, 0);
_delay(200);
```

**Derivation.** At the locked position the true $\theta_e$ is 0. The model requires:

$$
0 = \operatorname{wrap}\big(d\,p\,\theta_m^{*} - \theta_0\big)
\;\Rightarrow\;
\theta_0 = \operatorname{wrap}\big(d\,p\,\theta_m^{*}\big)
$$

This is exactly what `electricalAngle()` returns while `zero_electric_angle` is temporarily 0.

Because $\theta_0$ is defined modulo $2\pi$ in the electrical domain, any of the $p$ stable lock positions gives the same offset. No absolute mechanical reference is needed.

---

## 5. Error analysis specific to the AS5600

**Quantization in the electrical domain.** Mechanical error is multiplied by $p$:

$$
\delta\theta_e = p \cdot \frac{2\pi}{4096}
$$

| Pole pairs | Electrical resolution (AS5600, 12-bit) | Electrical resolution (14-bit sensor) |
|---|---|---|
| 7 | 0.62° | 0.15° |
| 11 | 0.97° | 0.24° |
| 14 | 1.23° | 0.31° |

**Effect of offset error on torque.** An offset error $\varepsilon$ projects current onto the d-axis:

$$
\tau \propto i_q\cos\varepsilon
$$

- 1° of error costs only 0.015% of torque.
- 10° of error costs 1.5% of torque, plus a parasitic $i_d = i\sin\varepsilon$ (17%).
- AS5600 quantization is therefore not the limit.

**The dominant real error sources** are:

- **Magnet eccentricity and axial gap.** These cause a 1st/2nd-harmonic angle error of up to ±1–2° mechanical, which becomes ±7–28° electrical at 7–14 pole pairs. This error is position-dependent, so a single-point $\theta_0$ is only correct at one rotor position.
- **Cogging and friction.** These stop the rotor short of true zero during the lock.
- **I²C latency.** Reading 2 bytes at 400 kHz takes about 100–150 µs. This causes phase lag at speed: $\Delta\theta_e = p\,\omega\,t_{read}$. Use `Wire.setClock(400000)` and keep loop timing deterministic.

---

## 6. Improved multi-point calibration (recommended for the AS5600)

Averaging the offset over all $p$ electrical zeros in both directions cancels cogging bias, friction hysteresis and the 1st-harmonic eccentricity error.

Use a **circular mean**, because angles wrap:

$$
\hat\theta_0 = \operatorname{atan2}