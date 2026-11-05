# Madgwick Filter: Quaternion Orientation Update (Gyroscope + Accelerometer)

I don't have context for your project's "Cycle 21" or your specific payload. This is the standard 6-DOF (IMU) Madgwick derivation, with notes on applying it to a gimbal. Adjust the frame conventions to match your hardware.

---

## 1. Conventions

- Quaternion ${}^S_E\hat q = [q_1\; q_2\; q_3\; q_4]^T$, with $q_1$ the scalar part. It describes the orientation of the Earth frame $E$ relative to the sensor frame $S$, which here is the gimbal payload IMU.
- Hamilton product: $\otimes$. Conjugate: $\hat q^* = [q_1\; -q_2\; -q_3\; -q_4]$.
- Gyroscope (rad/s): ${}^S\omega = [0\;\omega_x\;\omega_y\;\omega_z]$.
- Accelerometer, normalized: ${}^S\hat a = [0\;a_x\;a_y\;a_z]/\|a\|$.
- Gravity reference in the Earth frame: ${}^E\hat g = [0\;0\;0\;1]$.

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation measured by the gyroscope is:

$$
{}^S_E\dot q_{\omega,t} = \tfrac12\, {}^S_E\hat q_{est,t-1} \otimes {}^S\omega_t
$$

In expanded form:

$$
\dot q_\omega = \tfrac12
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\;\;q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\;\;q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\;\;q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

Integrating this alone accumulates drift from gyro bias and noise. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

Find the orientation that rotates the Earth gravity reference onto the measured acceleration direction:

$$
\min_{\hat q}\; f(\hat q, {}^E\hat g, {}^S\hat a),\qquad
f = \hat q^* \otimes {}^E\hat g \otimes \hat q - {}^S\hat a
$$

### 3.1 Objective function

With ${}^E\hat g = [0\,0\,0\,1]$, the vector part reduces to:

$$
f_g(\hat q, {}^S\hat a) =
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

### 3.2 Jacobian

$$
J_g(\hat q) = \frac{\partial f_g}{\partial \hat q} =
\begin{bmatrix}
-2q_3 & 2q_4 & -2q_1 & 2q_2\\
2q_2 & 2q_1 & 2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### 3.3 Gradient

$$
\nabla f = J_g^T f_g
$$

With $f_g = [f_1, f_2, f_3]^T$, the components are:

$$
\begin{aligned}
s_1 &= -2q_3 f_1 + 2q_2 f_2\\
s_2 &= \;\;2q_4 f_1 + 2q_1 f_2 - 4q_2 f_3\\
s_3 &= -2q_1 f_1 + 2q_4 f_2 - 4q_3 f_3\\
s_4 &= \;\;2q_2 f_1 + 2q_3 f_2
\end{aligned}
$$

### 3.4 Gradient-descent step

$$
\hat q_{\nabla,t} = \hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick's key argument is that one gradient step per sample is sufficient, provided the convergence rate matches the rate of physical orientation change. That is, choose $\mu_t = \alpha \|\dot q_{\omega,t}\|\Delta t$ with $\alpha > 1$.

---

## 4. Fusion

The gyro estimate and the gradient estimate are combined in a complementary manner. The optimal weighting balances gyro divergence against accelerometer convergence.

Choosing a large $\mu_t$ (so the step is dominated by the normalized gradient) simplifies the fused estimate to a single rate equation:

$$
\boxed{\;{}^S_E\dot q_{est,t} = {}^S_E\dot q_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

Integrate and normalize:

$$
\boxed{\;\hat q_{est,t} = \hat q_{est,t-1} + \dot q_{est,t}\,\Delta t,\qquad
\hat q_{est,t} \leftarrow \frac{\hat q_{est,t}}{\|\hat q_{est,t}\|}\;}
$$

### 4.1 Gain β

$\beta$ is the gyroscope measurement error expressed as a quaternion rate. If each gyro axis has zero-mean error of magnitude $\tilde\omega_\beta$:

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

---

## 5. Optional: Gyro Bias Compensation (ζ term)

The normalized gradient direction encodes the angular-rate error:

$$
{}^S\omega_{\varepsilon,t} = 2\,\hat q_{est,t-1}^* \otimes \frac{\nabla f}{\|\nabla f\|}
$$

Integrate this to estimate the bias, then correct the gyro reading:

$$
{}^S\omega_{b,t} = \zeta \sum_t {}^S\omega_{\varepsilon,t}\,\Delta t,\qquad
{}^S\omega_{c,t} = {}^S\omega_t - {}^S\omega_{b,t}
$$

Use the corrected rate $\omega_c$ in Section 2. The gain is:

$$
\zeta = \sqrt{\tfrac34}\;\tilde\omega_\zeta
$$

where $\tilde\omega_\zeta$ is the gyro bias drift rate.

---

## 6. Per-Sample Algorithm

1. Read $\omega$ and $a$. If $\|a\| = 0$, skip the correction and integrate the gyro only.
2. Normalize $a$.
3. Compute $\dot q_\omega = \tfrac12 \hat q \otimes [0,\omega]$.
4. Compute $f_g$ and $s = J_g^T f_g$, then normalize $s$.
5. Compute $\dot q = \dot q_\omega - \beta s$.
6. Integrate $q \mathrel{+}= \dot q\,\Delta t$, then normalize $q$.

---

## 7. Gimbal-Payload Considerations

- **Yaw is unobservable.** Gravity constrains only roll and pitch, so yaw will drift. Fix this with a magnetometer (the MARG variant, which adds $f_b$ and $J_b$ terms), gimbal motor encoders, or the host vehicle's heading.
- **Non-gravitational acceleration.** Vehicle maneuvers and centripetal acceleration from gimbal rotation corrupt the gravity reference. Two mitigations:
  - Gate or reduce $\beta$ when $\big|\|a\| - g\big|$ exceeds a threshold, e.g. 0.1 g.
  - Subtract the known centripetal term $\omega \times (\omega \times r)$ if the IMU is offset from the rotation axis by $r$.
- **Gain scheduling.** Use a high $\beta$ (around 0.5–2) at startup for fast convergence, then a low $\beta$ (around 0.01–0.1) for stabilization quality. For gimbal stabilization, a low steady-state $\beta$ keeps the gyro dominant in the control bandwidth.
- **Timing.** Use the measured $\Delta t$ per sample, not a nominal value, especially if the IMU runs asynchronously from the motor control loop.