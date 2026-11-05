# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU) Fusion for a Gimbal Payload

## 0. Conventions

- Quaternion: $\hat{q} = [q_0\; q_1\; q_2\; q_3]^T$, with scalar first.
- $^{S}_{E}\hat{q}$ is the orientation of the **Earth frame E relative to the sensor (payload) frame S**. This is Madgwick's convention.
- $\otimes$ is the Hamilton quaternion product, and $\hat{q}^*$ is the conjugate.
- Gyro measurement: $^{S}\omega = [0\;\omega_x\;\omega_y\;\omega_z]$ in rad/s.
- Accelerometer measurement: $^{S}a = [0\;a_x\;a_y\;a_z]$, normalized to unit length.

---

## 1. Orientation from the gyroscope (kinematics)

The quaternion rate of change driven by the measured angular rate is:

$$
^{S}_{E}\dot{q}_{\omega,t} = \tfrac{1}{2}\, ^{S}_{E}\hat{q}_{est,t-1} \otimes {}^{S}\omega_t
$$

Expanding the product gives the four component rates:

$$
\begin{aligned}
\dot q_0 &= \tfrac12(-q_1\omega_x - q_2\omega_y - q_3\omega_z)\\
\dot q_1 &= \tfrac12(\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y)\\
\dot q_2 &= \tfrac12(\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x)\\
\dot q_3 &= \tfrac12(\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x)
\end{aligned}
$$

Integrating these alone, $q_t = q_{t-1} + \dot q_\omega \Delta t$, accumulates gyro bias and noise, so the estimate drifts.

---

## 2. Orientation from the accelerometer (optimization problem)

When the payload is not accelerating, the accelerometer measures only gravity. The goal is to find the orientation that rotates the known Earth-frame gravity direction $^{E}\hat d$ onto the measured sensor-frame direction $^{S}\hat s$.

Gravity in the Earth frame is:

$$
^{E}\hat g = [0\;0\;0\;1].
$$

The objective function is the mismatch between rotated gravity and the measurement:

$$
f(\hat q, {}^{E}\hat d, {}^{S}\hat s) = \hat q^* \otimes {}^{E}\hat d \otimes \hat q - {}^{S}\hat s,
\qquad
\min_{\hat q \in \mathbb{R}^4} \tfrac12\|f\|^2
$$

Substituting $^{E}\hat d = {}^{E}\hat g$ and $^{S}\hat s = {}^{S}\hat a$, the rotated gravity vector is the third column of the rotation matrix. The residual becomes:

$$
f_g(\hat q, {}^{S}\hat a)=
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

Its Jacobian with respect to $[q_0, q_1, q_2, q_3]$ is:

$$
J_g(\hat q)=
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

The gradient of the cost $\tfrac12\|f_g\|^2$ is:

$$
\nabla f = J_g^T(\hat q)\, f_g(\hat q, {}^{S}\hat a)
$$

Written out explicitly, with $f_1, f_2, f_3$ the residual components:

$$
\begin{aligned}
\nabla f_0 &= -2q_2 f_1 + 2q_1 f_2\\
\nabla f_1 &= \;\;\,2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
\nabla f_2 &= -2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\nabla f_3 &= \;\;\,2q_1 f_1 + 2q_2 f_2
\end{aligned}
$$

One normalized gradient-descent step on this cost gives:

$$
^{S}_{E}q_{\nabla,t} = {}^{S}_{E}\hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

---

## 3. Fusion

Madgwick's key insight is that only one gradient step per sample is needed, as long as the convergence rate of that step matches the gyro's drift rate. The step size is chosen so that the accelerometer correction just cancels the worst-case gyro error.

The fused rate and the integration step are therefore:

$$
\boxed{\;^{S}_{E}\dot q_{est,t} = \tfrac12\,\hat q_{est,t-1}\otimes{}^{S}\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

$$
\boxed{\;\hat q_{est,t} = \frac{\hat q_{est,t-1} + {}^{S}_{E}\dot q_{est,t}\,\Delta t}{\left\|\hat q_{est,t-1} + {}^{S}_{E}\dot q_{est,t}\,\Delta t\right\|}\;}
$$

The final normalization keeps the quaternion on the unit sphere.

### Choosing β

Assume each gyro axis has a zero-mean error of magnitude $\tilde\omega_\beta$. The magnitude of the resulting quaternion-rate error sets β:

$$
\beta = \left\|\tfrac12\,\hat q \otimes [0\;\tilde\omega_\beta\;\tilde\omega_\beta\;\tilde\omega_\beta]\right\| = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

The choice of β trades off two behaviours:

- **Large β** makes the filter trust the accelerometer more. It converges faster but is noisier and more sensitive to linear acceleration.
- **Small β** makes the filter trust the gyro more. Output is smoother, but drift is corrected more slowly.

---

## 4. Optional: gyro bias compensation

The normalized gradient direction can be mapped back into an angular-rate error:

$$
^{S}\omega_{\varepsilon,t} = 2\,\hat q_{est,t-1}^* \otimes \frac{\nabla f}{\|\nabla f\|}
$$

This error is integrated into a bias estimate, which is then removed from the gyro reading:

$$
^{S}\omega_{b,t} = \zeta\sum_t {}^{S}\omega_{\varepsilon,t}\,\Delta t,
\qquad
^{S}\omega_{c,t} = {}^{S}\omega_t - {}^{S}\omega_{b,t}
$$

The bias gain is set from the expected bias drift rate $\dot{\tilde\omega}_\beta$:

$$
\zeta = \sqrt{\tfrac34}\,\dot{\tilde\omega}_\beta
$$

The bias-corrected rate $^{S}\omega_{c,t}$ is then used in place of $^{S}\omega_t$ in the fused update of Section 3.

---

## 5. Algorithm per sample

1. Normalize the accelerometer reading: $\hat a = a/\|a\|$. Skip the correction step if $\|a\| = 0$.
2. Compute the residual $f_g$, the Jacobian $J_g$, and the gradient $\nabla f = J_g^T f_g$, then normalize the gradient.
