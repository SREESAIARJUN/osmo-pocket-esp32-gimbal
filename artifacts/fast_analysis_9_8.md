# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## A correction first

The **AS5600 is a 12-bit encoder (4096 counts/rev)**, not 14-bit. The 14-bit parts are the AS5047P, AS5048A/B and AS5147. The derivation below works for any absolute magnetic sensor. Only the resolution numbers change, and I give both.

I don't know what "Cycle 9" refers to. It isn't a SimpleFOC term. If it's a stage in your own project or course, tell me and I'll map this onto it.

---

## 1. The model we are calibrating

The sensor reports a mechanical angle θ_m. FOC needs the electrical angle θ_e of the rotor d-axis (the permanent-magnet flux axis) relative to the phase-A axis:

$$
\theta_e = \text{norm}_{2\pi}\big(s \cdot p \cdot \theta_m - \theta_0\big)
$$

where:

| Symbol | Meaning | SimpleFOC variable |
|---|---|---|
| p | pole pairs | `motor.pole_pairs` |
| s ∈ {+1, −1} | sensor direction vs. electrical rotation direction | `motor.sensor_direction` (CW / CCW) |
| θ₀ | electrical offset: magnet mounting angle relative to the rotor d-axis | `motor.zero_electric_angle` |

This is exactly SimpleFOC's `FOCMotor::electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs) * sensor->getMechanicalAngle()
                         - zero_electric_angle);
}
```

The AS5600 magnet is glued on at an arbitrary rotation, so θ₀ is unknown. The motor's winding direction relative to the chip's CW/CCW convention is also unknown, so s is unknown. Calibration must find both.

---

## 2. Step 1: Direction (and pole-pair sanity) detection

**Idea:** drive the motor open-loop with a rotating voltage vector. The rotor follows the field (at low speed), so you know how much electrical angle you commanded. Measure how much mechanical angle the sensor reports.

The procedure:

1. Sweep the commanded electrical angle forward from 3π/2 to 3π/2 + 2π in small steps (SimpleFOC uses 500 steps, 2 ms each) at voltage `voltage_sensor_align`.
2. Record θ_m,mid.
3. Sweep back the same distance and record θ_m,end.

Then:

$$
\Delta\theta_m = \theta_{m,\text{end}} - \theta_{m,\text{mid}}
$$

**Direction.** The return sweep is a negative electrical rotation.
- If the sensor angle decreased (θ_end < θ_mid), the sensor counts in the same sense as the electrical rotation, so s = +1 (CW).
- Otherwise s = −1 (CCW).

**Pole-pair check.** One electrical revolution corresponds to 2π/p mechanical. So:

$$
|\Delta\theta_m| \cdot p \approx 2\pi
$$

SimpleFOC warns if |Δθ_m·p − 2π| > 0.5 rad.

It fails outright if |Δθ_m| < `MIN_ANGLE_DETECT_MOVEMENT`, meaning the rotor didn't move. Likely causes are too low a voltage, a stuck rotor, or a dead sensor. Useful side effect: you can estimate the pole pairs as p ≈ 2π / |Δθ_m|.

```cpp
// Simplified from BLDCMotor::alignSensor()
for (int i = 0; i <= 500; i++) {
  float a = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, a);
  sensor->update(); _delay(2);
}
sensor->update(); float mid = sensor->getAngle();
for (int i = 500; i >= 0; i--) {
  float a = _3PI_2 + _2PI * i / 500.0f;
  setPhaseVoltage(voltage_sensor_align, 0, a);
  sensor->update(); _delay(2);
}
sensor->update(); float end = sensor->getAngle();
setPhaseVoltage(0, 0, 0); _delay(200);

float moved = fabs(mid - end);
if (moved < MIN_ANGLE_DETECT_MOVEMENT) return 0;           // failed
sensor_direction = (mid < end) ? Direction::CCW : Direction::CW;
if (fabs(moved * pole_pairs - _2PI) > 0.5f) { /* PP check failed */ }
```

Use `getAngle()` (multi-turn), not `getMechanicalAngle()`. Otherwise the 0/2π wrap of the AS5600 can corrupt Δθ.

---

## 3. Step 2: Zero (offset) finding

### Why the command angle is 3π/2

`setPhaseVoltage(Uq, Ud, θ)` builds the stator voltage vector in the dq frame rotated by θ (inverse Park transform):

$$
V_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad V_\beta = U_d\sin\theta + U_q\cos\theta
$$

With U_d = 0, U_q = V and θ = 3π/2:

$$
V_\alpha = -V\sin(3\pi/2) = +V,\qquad V_\beta = V\cos(3\pi/2) = 0
$$

So the stator field points along the **α axis (electrical angle 0)**. At standstill, the rotor's d-axis (magnet flux) aligns with the stator field. The rotor therefore settles where **θ_e = 0 by definition**.

### Solving for θ₀

At that equilibrium:

$$
0 = s\,p\,\theta_m^{*} - \theta_0 \;\;\Rightarrow\;\; \theta_0 = \text{norm}_{2\pi}(s\,p\,\theta_m^{*})
$$

In code, this means computing the electrical angle with `zero_electric_angle = 0` and storing the result:

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                                  // let rotor settle
sensor->update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();      // = norm(s*p*θm*)
_delay(20);
setPhaseVoltage(0, 0, 0);
_delay(200);
```

Two details matter here:
- Direction must be found first, because θ₀ depends on s.
- The 700 ms wait lets oscillations die out. A lightly damped rotor will ring.

---

## 4. Error analysis for the AS5600

**Resolution**
- AS5600: 4096 counts, so 0.0879° mechanical per count. This becomes **p × 0.0879° electrical**. For a 7-pole-pair gimbal motor that is ≈ 0.62° electrical.
- For a 14-bit AS5048: 0.022° mechanical, ≈ 0.15° electrical at p = 7.

**Effect of an offset error ε on torque**

$$
i_q^{\text{actual}} = i_q\cos\varepsilon,\qquad i_d^{\text{parasitic}} = i_q\sin\varepsilon
$$

| ε (electrical) | Torque loss | Parasitic i_d |
|---|---|---|
| 5° | 0.4 % | 8.7 % |
| 20° | 6 % | 34 % (heating, and asymmetry between CW/CCW speed) |

Quantization is therefore never the problem. The real error sources are:

1. **Friction and cogging.** The rotor stops short of the true equilibrium, by an amount roughly τ_friction / (K_t·I).
2. **Load torque** during alignment.
3. **AS5600 non-linearity.** This is about ±1° mechanical for a well-centred diametric magnet, and worse when the magnet is off-axis. It gets multiplied by p.
4. **Wrong register or I²C staleness.**
   - Read `RAW ANGLE` (0x0C–0x0D), not `ANGLE` (0x0E–0x0F). The latter is affected by ZPOS/MPOS scaling and hysteresis.
   - Run I²C at 400 kHz–1 MHz.
   - The AS5600's analog/PWM outputs have additional filtering lag.

---

## 5. Improved algorithm: multi-point circular averaging

To cancel friction bias and average out sensor non-linearity, align at N electrical angles, approaching each from both directions. Then take the circular mean of the residuals:

$$
