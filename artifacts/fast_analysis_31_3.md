# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU) Fusion

**Context:** An IMU rigidly mounted on the gimbal payload, estimating the payload's orientation relative to Earth. Without a magnetometer, only roll and pitch are observable. Yaw drifts.

**Conventions**
- Quaternion: q = [q₀, q₁, q₂, q₃]ᵀ, with scalar first. It is a unit quaternion describing the Earth frame E relative to the sensor frame S, i.e. ^S_E q.
- ⊗ denotes the quaternion product, and q* the conjugate.
- Gyroscope measurement: ω = [0, ωₓ, ω_y, ω_z] in rad/s.
- Accelerometer measurement: a = [0, aₓ, a_y, a_z], normalized to unit length.

---

## 1. Gyroscope Path: Quaternion Kinematics

Orientation propagates according to the quaternion derivative:

$$
\dot{q}_{\omega,t} = \tfrac{1}{2}\, \hat{q}_{t-1} \otimes \omega_t
$$

Expanding the product gives:

$$
\dot{q}_\omega = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone, q_ω,t = q̂_{t−1} + q̇_ω Δt, is accurate over short horizons. Over time, however, it drifts because of gyro bias and noise.

---

## 2. Accelerometer Path: Gradient-Descent Orientation

**Problem formulation.** Find the q that rotates a known Earth-frame reference direction d̂_E onto the measured sensor-frame direction ŝ_S:

$$
\min_q f(q, \hat d_E, \hat s_S), \qquad f = q^* \otimes \hat d_E \otimes q - \hat s_S
$$

**Gravity reference.** Gravity defines the reference: d̂_E = g_E = [0, 0, 0, 1] and ŝ_S = a. Substituting gives the objective function:

$$
f_g(q, a) =
\begin{bmatrix}
2(q_1 q_3 - q_0 q_2) - a_x\\
2(q_0 q_1 + q_2 q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

The first three terms are the third column of the rotation matrix, i.e. the gravity direction predicted in the sensor frame.

**Jacobian.** Differentiate with respect to [q₀, q₁, q₂, q₃]:

$$
J_g(q) =
\begin{bmatrix}
-2q_2 & 2q_3 & -2q_0 & 2q_1\\
2q_1 & 2q_0 & 2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

**Gradient.** For the cost ½‖f‖², the gradient is ∇f = J_gᵀ f_g. Written out:

$$
\nabla f =
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2\\
\ \ 2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\ \ 2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

**Single descent step.** One gradient step per sample suffices if the step size μ_t is at least as fast as the true rotation rate:

$$
q_{\nabla,t} = \hat q_{t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}, \qquad \mu_t = \alpha\,\|\dot q_{\omega,t}\|\,\Delta t,\ \ \alpha > 1
$$

---

## 3. Fusion

**Weighted combination.** Blend the two estimates:

$$
\hat q_t = \gamma\, q_{\nabla,t} + (1-\gamma)\, q_{\omega,t}
$$

**Choosing γ.** Pick γ so that the gyro's divergence rate β balances the convergence rate of the gradient step:

$$
(1-\gamma)\beta = \gamma\,\frac{\mu_t}{\Delta t} \;\Rightarrow\; \gamma = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

**Large-μ simplification.** Because α is large, μ_t is large, so γ ≈ βΔt/μ_t ≪ 1. Substituting this, and noting that the q̂_{t−1} terms dominate, the blend collapses to a single rate equation. This is the **Madgwick update**:

$$
\boxed{\dot{\hat q}_t = \tfrac12\, \hat q_{t-1}\otimes\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}}
$$

$$
\boxed{\hat q_t = \frac{\hat q_{t-1} + \dot{\hat q}_t\,\Delta t}{\left\|\hat q_{t-1} + \dot{\hat q}_t\,\Delta t\right\|}}
$$

The gradient term is a unit-direction correction. It pulls the orientation toward agreement with gravity at rate β, while the gyro term handles fast dynamics.

---

## 4. Choosing β

Let ω̃_β be the per-axis gyro measurement error, with the same magnitude on all three axes (in rad/s). The magnitude of the resulting quaternion-rate error is:

$$
\left\|\tfrac12\,\hat q\otimes[0,\tilde\omega_\beta,\tilde\omega_\beta,\tilde\omega_\beta]\right\| = \tfrac12\sqrt3\,\tilde\omega_\beta
\;\Rightarrow\;
\boxed{\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta}
$$

The trade-off:
- **Larger β:** faster drift correction, but more accelerometer noise and vibration leaks into the estimate.
- **Smaller β:** smoother output, but slower correction.

---

## 5. Optional Gyro Bias Compensation

The normalized gradient direction can be mapped back into an angular-rate error:

$$
\omega_{\varepsilon,t} = 2\,\hat q_{t-1}^*\otimes \frac{\nabla f}{\|\nabla f\|}
$$

Integrate this error into a bias estimate, then subtract it from the gyro reading:

$$
\omega_{b,t} = \zeta\sum \omega_{\varepsilon,t}\Delta t
$$

$$
\omega_{c,t} = \omega_t - \omega_{b,t}
$$

Use ω_c in Section 1. A typical choice is ζ = √(3/4)·ω̇̃_ζ, where ω̇̃_ζ is the gyro bias drift rate.

---

## 6. Per-Sample Algorithm

1. Read ω (rad/s) and a. If ‖a‖ = 0, skip the correction step.
2. Normalize a.
3. Compute q̇_ω = ½ q ⊗ ω.
4. Compute f_g and ∇f = J_gᵀ f_g, then normalize ∇f.
5. Combine: q̇ = q̇_ω − β∇f̂.
6. Integrate: q ← q + q̇Δt.
7. Normalize q.

---

## 7. Gimbal-Payload Considerations

- **Linear acceleration:** Platform maneuvers corrupt the gravity reference. Gate or scale β when |‖a_raw‖ − g| exceeds a threshold, for example by setting β → 0 during hard maneuvers.
- **Motor vibration:** Low-pass filter the accelerometer before fusion, and keep β small (about 0.01–0.05). The gimbal's control loop relies on gyro-dominated, low-latency attitude.
- **Yaw is unobservable:** ∂f_g/∂(yaw) = 0, so he