# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

I don't have the details of your "Cycle 11" spec. The derivation below is the standard 6-DOF Madgwick formulation, with notes for a gimbal-mounted IMU at the end.

---

## 1. Conventions

- Quaternion: **q** = [q₁ q₂ q₃ q₄]ᵀ = [w x y z]ᵀ, unit norm.
- **q** = ˢₑ**q̂** describes the orientation of the Earth frame *E* relative to the sensor frame *S*.
- Quaternion product ⊗. Conjugate **q*** = [q₁ −q₂ −q₃ −q₄].
- Gyroscope measurement: ˢ**ω** = [0 ωₓ ω_y ω_z] in rad/s.
- Accelerometer measurement: ˢ**a** = [0 aₓ a_y a_z], normalized to unit length.

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation is:

$$
\dot{\mathbf q}_{\omega,t} = \tfrac12\, \hat{\mathbf q}_{t-1} \otimes {}^S\boldsymbol\omega_t
$$

Expanded:

$$
\dot{\mathbf q}_\omega = \tfrac12
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\;\;q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\;\;q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\;\;q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

Integrating this alone accumulates drift from gyro bias and noise. The accelerometer is used to correct it.

---

## 3. Accelerometer Correction as an Optimization Problem

Find the orientation **q** that rotates the known Earth gravity direction into the measured sensor-frame direction.

**Reference gravity direction:** ᴱ**g** = [0 0 0 1].

**Objective function:**

$$
\mathbf f(\mathbf q, {}^E\mathbf g, {}^S\mathbf a) = \mathbf q^* \otimes {}^E\mathbf g \otimes \mathbf q - {}^S\mathbf a
$$

Expanding the rotation of [0 0 1] gives the third column of the rotation matrix:

$$
\mathbf f_g(\mathbf q, {}^S\mathbf a) =
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

**Jacobian:** ∂**f**/∂**q**:

$$
\mathbf J_g(\mathbf q)=
\begin{bmatrix}
-2q_3 & \;\;2q_4 & -2q_1 & 2q_2\\
\;\;2q_2 & \;\;2q_1 & \;\;2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

**Gradient of the cost** ½‖**f**‖²:

$$
\nabla \mathbf f = \mathbf J_g^{\mathsf T}(\hat{\mathbf q}_{t-1})\, \mathbf f_g(\hat{\mathbf q}_{t-1}, {}^S\mathbf a_t)
$$

**Explicit components.** Multiplying out and using ‖**q**‖ = 1 gives the efficient form (up to a constant factor, which is irrelevant after normalization):

$$
\begin{aligned}
s_1 &= 4q_1q_3^2 + 2q_3a_x + 4q_1q_2^2 - 2q_2a_y\\
s_2 &= 4q_2q_4^2 - 2q_4a_x + 4q_1^2q_2 - 2q_1a_y - 4q_2 + 8q_2^3 + 8q_2q_3^2 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 + 2q_1a_x + 4q_3q_4^2 - 2q_4a_y - 4q_3 + 8q_3q_2^2 + 8q_3^3 + 4q_3a_z\\
s_4 &= 4q_2^2q_4 - 2q_2a_x + 4q_3^2q_4 - 2q_3a_y
\end{aligned}
$$

A full gradient descent would iterate many times per sample. Madgwick's insight is that a **single step per sample** suffices, provided the step rate exceeds the rate of orientation change.

---

## 4. Fusion

The normalized gradient direction is subtracted from the gyro rate, scaled by β:

$$
\boxed{\;\dot{\hat{\mathbf q}}_{t} = \dot{\mathbf q}_{\omega,t} \;-\; \beta\,\frac{\nabla \mathbf f}{\lVert \nabla \mathbf f \rVert}\;}
$$

**Discrete update:**

$$
\boxed{\;\hat{\mathbf q}_t = \hat{\mathbf q}_{t-1} + \dot{\hat{\mathbf q}}_t\,\Delta t, \qquad \hat{\mathbf q}_t \leftarrow \frac{\hat{\mathbf q}_t}{\lVert\hat{\mathbf q}_t\rVert}\;}
$$

### Choosing β

β is the gyro measurement error expressed as a quaternion rate:

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

Here ω̃_β is the expected gyro error magnitude in rad/s, per axis.

- **Larger β:** faster convergence, but more accelerometer noise and linear-acceleration error passes through.
- **Smaller β:** smoother output, but more drift.

Typical values are 0.03–0.1. Use a larger β during startup for fast convergence, then reduce it.

### Optional gyro-bias compensation

Estimate the bias drift from the error direction:

$$
\boldsymbol\omega_{\varepsilon} = 2\,\hat{\mathbf q}^*_{t-1}\otimes \frac{\nabla\mathbf f}{\lVert\nabla\mathbf f\rVert}
$$

$$
\boldsymbol\omega_b \mathrel{+}= \zeta\,\boldsymbol\omega_\varepsilon\,\Delta t
$$

Then use ω − ω_b in Step 2. The bias gain is ζ = √(3/4)·(estimated bias drift rate).

---

## 5. Algorithm Summary (per sample)

```
a  = a / |a|                         # skip correction if |a| ≈ 0
qDot = 0.5 * q ⊗ [0, gx, gy, gz]
s  = J_gᵀ(q) · f_g(q, a)             # or explicit s1..s4 above
s  = s / |s|
qDot -= beta * s
q  += qDot * dt
q  = q / |q|
```

---

## 6. Gimbal Payload Considerations

1. **Yaw is unobservable.** Gravity only constrains roll and pitch, so heading drifts at the gyro-bias rate. Correct yaw with a magnetometer (MARG variant; magnetically noisy near gimbal motors), the vehicle heading, or the gimbal motor encoders.

2. **Linear acceleration corrupts the gravity reference.** Vehicle acceleration and centripetal effects from gimbal rotation (an IMU offset r from the pivot sees ω×(ω×r) + α×r) both bias the estimate. Mitigations:
   - Gate the correction: set β → 0 when |‖a‖ − 1 g| > ~0.1 g, or when ‖ω‖ is large.
   - Subtract known lever-arm terms.

3. **Rate and latency.** Run at the gyro ODR (≥ 500 Hz to 1 kHz for stabilization loops). Use the measured Δt, not the nominal one.

4. **Frame alignment.** Rotate raw sensor axes into the payload body frame before the update. A mismatched sign shows up as divergence in a single axis.

5. **Vibration.** Anti-alias filter the accelerometer before normalization. Motor PWM harmonics can alias into the gravity estimate.

If you share the Cycle 11 requirements (sample rate, sensor noise specs, whether a magnetometer or encoders are available), I can compute a specific β and ζ and adapt the update for your configuration.