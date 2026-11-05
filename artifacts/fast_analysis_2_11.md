# Electrical Angle Offset Calibration for the AS5600 in SimpleFOC

## 0. Two corrections first

- **The AS5600 is 12-bit, not 14-bit.** It gives 4096 counts per revolution. The 14-bit parts are the AS5047, AS5048 and AS5147. The derivation below works for any resolution N. Only the numeric error budget in section 5 changes.
- **I don't know what "Cycle 2" refers to.** It may be a course module, a previous conversation, or a revision of your own. I've treated it as the full calibration pass. Tell me if you meant something specific, such as a second calibration iteration or SimpleFOC v2 behaviour.

---

## 1. The model

The sensor reads a mechanical angle:

$$
\theta_m = \frac{2\pi}{4096}\cdot \text{RAW\_ANGLE} \quad (\text{reg } 0x0C/0x0D,\ 12\text{-bit})
$$

The field-oriented control (FOC) transforms need the rotor flux (d-axis) angle in electrical radians:

$$
\theta_e = \big(s\cdot p\cdot \theta_m - \theta_{0}\big) \bmod 2\pi
$$

where:

| Symbol | Meaning | Source |
|---|---|---|
| p | pole pairs | given; can be sanity-checked |
| s | sensor direction, ±1 | measured (`sensor_direction`) |
| θ₀ | `zero_electric_angle` | measured |

This matches SimpleFOC's `electricalAngle()`:

```cpp
float FOCMotor::electricalAngle(){
  return _normalizeAngle((float)(sensor_direction * pole_pairs)
                         * sensor->getMechanicalAngle() - zero_electric_angle);
}
```

The magnet is glued on at an arbitrary rotation, and the AS5600 ZPOS register is arbitrary unless you program it. So θ₀ is unknown and must be found experimentally.

---

## 2. Why SimpleFOC applies the voltage at 3π/2

`setPhaseVoltage(Uq, Ud, θ)` performs an inverse Park transform followed by an inverse Clarke transform. The inverse Park step is:

$$
U_\alpha = U_d\cos\theta - U_q\sin\theta,\qquad
U_\beta = U_d\sin\theta + U_q\cos\theta
$$

Calibration calls it with Uq = V_align, Ud = 0 and θ = 3π/2:

$$
U_\alpha = -V_{align}\cdot\sin(3\pi/2) = +V_{align},\qquad U_\beta = V_{align}\cdot\cos(3\pi/2)=0
$$

So the stator field points exactly along the α-axis (phase A). A permanent-magnet rotor with no load settles with its d-axis on the stator field, which defines θ_e = 0.

Reading the sensor at that equilibrium and applying the definition gives:

$$
0 = s\,p\,\theta_m^{*} - \theta_0 \;\;\Rightarrow\;\; \boxed{\theta_0 = (s\,p\,\theta_m^{*}) \bmod 2\pi}
$$

This is why the library simply stores `zero_electric_angle = electricalAngle()` with the offset temporarily set to 0. Using "Uq at 3π/2" rather than "Ud at 0" is just a convention. It lets the same voltage-limited Uq path be reused.

---

## 3. Step 1: direction and pole-pair check

The steps are:

1. Drive an open-loop electrical angle ramp over one electrical turn: θ_cmd goes from 3π/2 to 3π/2 + 2π in about 500 steps.
2. Read θ_m at the start and at the end.
3. Compute the mechanical travel:

$$
\Delta\theta_m = \theta_m^{end} - \theta_m^{start},\qquad
s = \operatorname{sign}(\Delta\theta_m)
$$

4. Check the pole-pair count. One electrical turn should move the rotor 2π/p mechanically, so:

$$
\hat p = \frac{2\pi}{|\Delta\theta_m|}
$$

If |p̂ − p| > 0.5, warn ("PP check: fail").

Read Δθ_m through SimpleFOC's `getAngle()`, which is multi-turn and unwrapped. A single wrapped reading will break when the travel crosses the AS5600's 4095 → 0 boundary.

---

## 4. Step 2: zero finding

### 4.1 Baseline (what SimpleFOC does)

```cpp
setPhaseVoltage(voltage_sensor_align, 0, _3PI_2);
_delay(700);                    // let rotor settle
sensor->update();
zero_electric_angle = 0;
zero_electric_angle = electricalAngle();   // = s*p*θm* mod 2π
setPhaseVoltage(0, 0, 0);
```

### 4.2 Improved robust version

A single reading is biased by three things:

- cogging and friction, which leave the rotor short of the true equilibrium,
- AS5600 noise and hysteresis (the CONF HYST bits),
- magnet eccentricity, which adds a sinusoidal error per mechanical revolution.

The fix is to sample K electrical positions in both directions and take a circular mean of the residuals.

For each commanded electrical angle φ_k = 2πk/K, approached once from each direction:

$$
r_k = \big(s\,p\,\theta_{m,k} - \varphi_k\big) \bmod 2\pi
$$

The offset is the circular mean of the residuals, which is safe across the wrap:

$$
\theta_0 = \operatorname{atan2}\!\Big(\sum_k \sin r_k,\ \sum_k \cos r_k\Big)
$$

Approaching from both directions cancels the friction lag ±δ to first order. Spreading the samples over all electrical positions, ideally across every pole pair (K = n·p), averages out eccentricity and cogging harmonics.

```cpp
float calibrateZero(BLDCMotor& m, int K /* e.g. 4*pole_pairs */, float V){
  float S = 0, C = 0;
  for (int dir = -1; dir <= 1; dir += 2) {
    for (int k = 0; k < K; k++) {
      int idx = (dir > 0) ? k : (K - 1 - k);
      float phi = _2PI * idx / K;
      // ramp smoothly to phi to avoid jumps
      for (int j = 0; j < 50; j++) {
        float a = phi - dir * _2PI / K * (1 - j / 50.0f);
        m.setPhaseVoltage(V, 0, a + _3PI_2);   // +3π/2 → field at electrical angle a
        _delay(2);
      }
      _delay(150);
      float th = 0;                            // average out sensor noise
      for (int n = 0; n < 16; n++) {
        m.sensor->update();
        th += m.sensor->getMechanicalAngle();
        _delay(1);
      }
      th /= 16;
      float r = _normalizeAngle(m.sensor_direction * m.pole_pairs * th - phi);
      S += _sin(r);
      C += _cos(r);
    }
  }
  m.setPhaseVoltage(0, 0, 0);
  return _normalizeAngle(atan2(S, C));
}
```

Two cautions on this code:

- The 16-sample average is a linear mean. It assumes the readings don't straddle 0/2π. Use a circular mean here too if they might.
- The spread of the r_k values doubles as a quality metric. A large spread points to magnet eccentricity or a wrong pole-pair count.

Use the result with `motor.zero_electric_angle = θ0; motor.sensor_direction = s;`. You can also store it in flash and skip calibration via `motor.initFOC()` with these values preset.

---

## 5. Error budget (AS5600, 12-bit)

**Quantization.** The mechanical step is 2π/4096 = 0.088°. This is multiplied by p in electrical degrees. For p = 7, that gives about 0.62° electrical.

**Torque loss from offset error ε.** Torque scales with cos ε:

| ε (electrical) | Torque loss |
|---|---|
| 0.62° (quantization only) | ≈ 0.006 % (negligible) |
| 10° | ≈ 1.5 %, plus d-axis current waste |

So quantization is not the limit. The bottlenecks are mechanical: friction lag and eccentricity. That is what the bidirectional, multi-point method in 4.2 addresses.

**AS5600-specific notes:**

- **Disable hysteresis.** Set CONF.HYST = 00. Otherwise the reading sticks by up to 3 LSB, which adds about 1.9° electrical error at p = 7.
- **Use RAW_ANGLE, not ANGLE.** ANGLE is remapped by ZPOS/MPOS. The software offset θ₀ makes ZPOS burning (OTP, limited writes) unnecessary.
- **Mind I²C latency.** At 400 kHz a read takes about 100 µs. This doesn't affect static calibration. At speed it causes a lag of about ω_e·t_read, which you should compensate separately. It is not part of θ₀.
- **Choose V_align carefully.** Use enough voltage to overpower cogging (typically 1–3 V or about 10–20 % of rated current), but stay low enough to avoid heating the motor.

---

##