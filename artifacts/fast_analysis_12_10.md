# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## A correction first

The **AS5600 is a 12-bit encoder** (4096 counts/rev), not 14-bit. The 14-bit parts are the AS5047P/D and AS5048A/B. The calibration algorithm is the same for all of them. Only the counts-per-revolution (CPR) and the resulting quantization error change, and both cases are covered below.

I don't recognize "Cycle 12" as a SimpleFOC term. If it refers to a specific course or document, the derivation below should still map onto it.

---

## 1. The problem

Field-oriented control (FOC) needs the **electrical angle** θₑ of the rotor flux (d-axis) relative to stator phase A. The encoder gives a **mechanical angle** θₘ with an arbitrary zero, because the magnet is glued on at a random orientation.

The relationship is:

$$
\theta_e = \text{norm}_{2\pi}\big(d \cdot p \cdot \theta_m - \theta_0\big)
$$

where:

| Symbol | Meaning | SimpleFOC variable |
|---|---|---|
| p | pole pairs | `pole_pairs` |
| d ∈ {+1, −1} | sensor direction | `sensor_direction` (CW/CCW) |
| θ₀ | electrical zero offset | `zero_electric_angle` |
| θₘ | mechanical angle | `sensor->getMechanicalAngle()` |

Calibration therefore has two unknowns to find: **d** and **θ₀**. The pole-pair count p is user-supplied but can be sanity-checked along the way.

---

## 2. Physical principle: forcing a known electrical angle

`setPhaseVoltage(Uq, Ud, angle_el)` performs an inverse Park transform:

$$
U_\alpha = U_d\cos\phi - U_q\sin\phi,\qquad U_\beta = U_d\sin\phi + U_q\cos\phi
$$

With Ud = 0 and Uq = V, the stator voltage vector points at φ + π/2. With no load, the permanent-magnet rotor's d-axis aligns with the stator field vector. It is a stable equilibrium, like a stepper motor holding a step.

SimpleFOC chooses φ = 3π/2:

$$
\phi + \tfrac{\pi}{2} = 2\pi \equiv 0
$$

So the field points along the α-axis (phase A), and the rotor settles at **true θₑ = 0**.

---

## 3. Step A: direction detection (and pole-pair check)

The algorithm sweeps the forced electrical angle through one full electrical revolution, first forward and then backward:

```
for i in 0..500:  φ = 3π/2 + 2π·i/500;  setPhaseVoltage(V_align, 0, φ); sensor.update(); delay(2ms)
θ_mid = sensor.getAngle()
for i in 500..0:  (same, backward)
θ_end = sensor.getAngle()
```

**Direction.** One electrical revolution moves the rotor exactly 2π/p mechanically. The sign of the motion gives d:

- θ_mid > θ_end → d = CW (+1)
- θ_mid < θ_end → d = CCW (−1)
- θ_mid = θ_end → **fail**. The motor did not move: V_align is too low, there is a wiring fault, or the sensor is not updating.

**Pole-pair check.** The measured movement is Δ = |θ_mid − θ_end|, so:

$$
\hat p = \frac{2\pi}{\Delta}
$$

SimpleFOC flags `PP check: fail` if |p·Δ − 2π| > 0.5 rad. The usual causes are a wrong `pole_pairs` value or a load and friction large enough to stop the rotor from following.

---

## 4. Step B: zero offset

The rotor is held at the known electrical angle and the encoder is read:

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);   // rotor → θe = 0
_delay(700);                                        // let it settle
sensor->update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();            // = norm(d·p·θm)
setPhaseVoltage(0, 0, 0);
```

**Derivation.** At equilibrium the true θₑ = 0. Substituting into the model:

$$
0 = d\,p\,\theta_{m,\text{align}} - \theta_0
\;\Rightarrow\;
\boxed{\theta_0 = \text{norm}_{2\pi}\big(d\,p\,\theta_{m,\text{align}}\big)}
$$

That is exactly what `electricalAngle()` returns when `zero_electric_angle` is temporarily 0.

The offset is found modulo 2π/p mechanically. This is fine because all p electrical periods are equivalent for commutation.

---

## 5. AS5600-specific error analysis

**Quantization.**

| Encoder | Mechanical LSB | Electrical LSB, p = 7 |
|---|---|---|
| AS5600 (12-bit) | 360/4096 = 0.088° | 0.615° |
| AS5047/AS5048 (14-bit) | 0.022° | 0.154° |

**Effect of an offset error δ.** An offset error rotates the current vector off the q-axis:

- Torque scales by cos δ.
- A spurious d-current appears, proportional to sin δ.

For δ = 1°, the torque loss is about 0.015%, which is negligible. The real error sources are mechanical, not quantization:

- **Cogging and friction.** The rotor stops short of the true equilibrium. The error is roughly the friction torque divided by the holding torque. Raising `voltage_sensor_align` reduces it, but watch current and heating.
- **Magnet eccentricity and I²C noise on the AS5600.** These cause ±0.1–0.5° mechanical error, which is multiplied by p electrically.
- **Hysteresis and filtering.** Set the AS5600 CONF register so that HYST = 0 and the slow filter is off, or accept the lag. Read the RAW ANGLE register (0x0C/0x0D) rather than the scaled ANGLE register (0x0E/0x0F) if you have programmed ZPOS/MPOS.

---

## 6. Improved (robust) zero-finding

Instead of a single sample at φ = 3π/2, sample N electrical positions across all p pole pairs, in both sweep directions. Then take the **circular mean** of the residuals. This averages out cogging, eccentricity, and friction bias, because forward and backward lag cancel.

```cpp
float s = 0, c = 0;
const int N = 12 * motor.pole_pairs;
for (int dir = 0; dir < 2; dir++) {
  for (int k = 0; k < N; k++) {
    int idx = dir ? (N - 1 - k) : k;
    float phi = _2PI * idx / motor.pole_pairs / 12.0f * motor.pole_pairs;  // = 2π·idx/12
    motor.setPhaseVoltage(V_align, 0, phi + _3PI_2);   // force θe = phi
    for (int j = 0; j < 50; j++) { sensor.update(); _delay(2); }  // settle
    float r = motor.sensor_direction * motor.pole_pairs * sensor.getMechanicalAngle() - phi;
    s += sinf(r);  c += cosf(r);
  }
}
motor.zero_electric_angle = _normalizeAngle(atan2f(s, c));
motor.setPhaseVoltage(0, 0, 0);
```

Each sample gives θ₀ estimate rₖ = d·p·θₘ,ₖ − φₖ. The `atan2` of the summed sines and cosines is the wrap-safe mean. The spread of rₖ is also a useful diagnostic: a large variation means magnet misalignment or a wrong p.

---

## 7. Skipping calibration on later boots

After one successful run, print the values and hard-code them:

```cpp
motor.zero_electric_angle = 2.15f;          // from Serial output
motor.sensor_direction    = Direction::CW;
motor.initFOC();                            // skips alignment
```

This remains valid as long as the magnet does not move relative to the shaft and the phase wiring order is unchanged. Swap