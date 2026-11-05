# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Form)

**Assumptions:** No prior Cycle 1–19 context was provided, so I use standard conventions. The quaternion is **q** = [q₀ q₁ q₂ q₃]ᵀ (scalar first). It rotates the Earth frame E into the sensor/payload frame S. ⊗ is the Hamilton product and q* is the conjugate.

---

## 1. Gyroscope Propagation (Rate Kinematics)

The payload gyro measures angular rate in the sensor frame:

$$
{}^S\boldsymbol{\omega} = [0,\ \omega_x,\ \omega_y,\ \omega_z]
$$

The quaternion derivative from the angular rate is:

$$
\dot{\mathbf q}_{\omega,t} = \tfrac12\, \hat{\mathbf q}_{t-1} \otimes {}^S\boldsymbol\omega_t
$$

Expanded:

$$
\dot{\mathbf q}_\omega = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone drifts, because gyro noise and bias accumulate. The accelerometer is used to correct that drift.

---

## 2. Accelerometer Correction as an Optimization Problem

Find the orientation **q** that rotates the known gravity reference into the measured (normalized) acceleration.

- Gravity reference in the Earth frame: ${}^E\mathbf g = [0,0,0,1]$
- Measured acceleration in the sensor frame: ${}^S\mathbf a = [0,a_x,a_y,a_z]$, with ‖a‖ = 1

The objective is:

$$
\min_{\mathbf q}\ \tfrac12\|\mathbf f(\mathbf q)\|^2,\qquad
\mathbf f(\mathbf q)=\mathbf q^*\otimes{}^E\mathbf g\otimes\mathbf q-{}^S\mathbf a
$$

Expanding the quaternion products gives the residual vector:

$$
\mathbf f_g(\mathbf q,{}^S\mathbf a)=
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

Its Jacobian with respect to [q₀ q₁ q₂ q₃] is:

$$
\mathbf J_g=
\begin{bmatrix}
-2q_2 & \ \ 2q_3 & -2q_0 & 2q_1\\
\ \ 2q_1 & \ \ 2q_0 & \ \ 2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

The gradient of the cost is:

$$
\nabla f = \mathbf J_g^{T}\mathbf f_g
$$

Written out element by element (the efficient implementation form):

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_1^2q_2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

*Check of s₀:* −2q₂f₁ + 2q₁f₂ = (4q₀q₂² − 4q₁q₂q₃ + 2q₂aₓ) + (4q₀q₁² + 4q₁q₂q₃ − 2q₁a_y). The cross terms cancel, giving the expression above.

---

## 3. Gradient-Descent Step

A gradient-descent step on the quaternion is:

$$
\mathbf q_{\nabla,t} = \hat{\mathbf q}_{t-1} - \mu_t\,\frac{\nabla f}{\|\nabla f\|}
$$

Madgwick's argument is that one iteration per sample is enough, provided the step size μₜ keeps pace with the orientation change from the gyro: μₜ = α‖q̇_ω‖Δt, with α > 1.

The gyro and gradient estimates are then fused with a weight γ:

$$
\mathbf q_{est} = \gamma\,\mathbf q_\nabla + (1-\gamma)\,\mathbf q_\omega
$$

Choosing γ so that the gyro divergence rate equals the gradient convergence rate, and taking large α (so γ ≈ βΔt/μₜ ≈ 0), the fusion collapses to a single rate equation.

---

## 4. Fused Update Equations

$$
\boxed{\dot{\hat{\mathbf q}}_{t} = \tfrac12\,\hat{\mathbf q}_{t-1}\otimes{}^S\boldsymbol\omega_t\;-\;\beta\,\frac{\nabla f}{\|\nabla f\|}}
$$

$$
\boxed{\hat{\mathbf q}_t = \frac{\hat{\mathbf q}_{t-1} + \dot{\hat{\mathbf q}}_t\,\Delta t}{\left\|\hat{\mathbf q}_{t-1} + \dot{\hat{\mathbf q}}_t\,\Delta t\right\|}}
$$

The filter gain β is set from the gyro measurement error:

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

Here ω̃_β is the expected gyro error magnitude per axis (rad/s). The factor √(3/4) comes from mapping a rate error on 3 axes into quaternion-derivative magnitude.

Interpretation: β is the only tuning knob.
- A larger β trusts the accelerometer more, giving faster convergence but more noise.
- A smaller β trusts the gyro more.

---

## 5. Per-Sample Algorithm

```
normalize a  (skip correction if |a| == 0)
qDot = 0.5 * q ⊗ [0, ωx, ωy, ωz]
s = [s0 s1 s2 s3] from Section 2;  s /= |s|
qDot -= β * s
q += qDot * dt
q /= |q|
```

---

## 6. Gimbal-Payload Considerations

- **Yaw is unobservable.** With gravity only, the gradient has no component about the vertical axis, so heading drifts at the gyro bias rate. Add a magnetometer (MARG form) or use the gimbal encoders and host-vehicle heading.
- **Linear acceleration corrupts the gravity reference.** Vehicle maneuvers and gimbal slewing add acceleration on top of gravity. Mitigate this by:
  - gating or reducing β when |‖a_raw‖ − g| exceeds a threshold, or
  - subtracting the known centripetal and tangential terms from the gimbal kinematics.
- **Gyro bias.** Optionally add Madgwick's bias compensation, using the error rate ω_err = 2 q̂* ⊗ (∇f/‖∇f‖):
  - ω_b ← ω_b + ζ ω_err Δt, with ζ ≈ √(3/4) ω̇̃_β
  - then subtract ω_b from ω before propagation.
- **Gain scheduling.** Use a high β (≈0.5–1) at startup for fast alignment, then drop to about 0.03–0.1 for steady-state stabilization.
- **Sampling.** Run at the gyro rate (≥