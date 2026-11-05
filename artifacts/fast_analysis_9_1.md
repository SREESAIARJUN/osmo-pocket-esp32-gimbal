# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

*I don't have your project's "Cycle 9" specifications, such as sensor part numbers, sample rate, or frame conventions. The derivation below is general. Map the axes to your gimbal's IMU mounting.*

---

## 1. Conventions

- Quaternion: $\mathbf{q} = [q_0, q_1, q_2, q_3]^T$ (scalar first), with $\|\mathbf{q}\| = 1$.
- $\mathbf{q} \equiv {}^S_E\hat{\mathbf{q}}$ describes the orientation of the Earth frame E relative to the sensor frame S.
- Product: $\otimes$ is the Hamilton product. Conjugate: $\mathbf{q}^* = [q_0, -q_1, -q_2, -q_3]$.
- Gyroscope (rad/s, sensor frame): ${}^S\boldsymbol{\omega} = [0, \omega_x, \omega_y, \omega_z]$.
- Accelerometer (normalized): ${}^S\hat{\mathbf{a}} = [0, a_x, a_y, a_z]$.
- Earth gravity reference: ${}^E\hat{\mathbf{g}} = [0, 0, 0, 1]$.

---

## 2. Gyroscope Orientation Rate

Quaternion kinematics give the rate of change of orientation:

$$
{}^S_E\dot{\mathbf{q}}_{\omega,t} = \tfrac{1}{2}\, {}^S_E\hat{\mathbf{q}}_{t-1} \otimes {}^S\boldsymbol{\omega}_t
$$

Expanding the Hamilton product:

$$
\dot{\mathbf{q}}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z \\
\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y \\
\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x \\
\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone accumulates drift from gyro bias and noise. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

The goal is to find the $\mathbf{q}$ that rotates the Earth gravity reference into the measured sensor-frame acceleration:

$$
\min_{\mathbf{q}} \; \tfrac{1}{2}\|\mathbf{f}(\mathbf{q}, {}^E\hat{\mathbf{g}}, {}^S\hat{\mathbf{a}})\|^2
$$

The objective function is the error between rotated gravity and the measurement:

$$
\mathbf{f}(\mathbf{q}, {}^E\hat{\mathbf{d}}, {}^S\hat{\mathbf{s}}) = \mathbf{q}^* \otimes {}^E\hat{\mathbf{d}} \otimes \mathbf{q} - {}^S\hat{\mathbf{s}}
$$

### 3.1 Specialize to gravity, $\hat{\mathbf{g}} = [0,0,0,1]$

Rotating the unit z-vector into the sensor frame gives the third column of the rotation matrix. This yields:

$$
\mathbf{f}_g(\mathbf{q}, {}^S\hat{\mathbf{a}}) =
\begin{bmatrix}
2(q_1 q_3 - q_0 q_2) - a_x \\
2(q_0 q_1 + q_2 q_3) - a_y \\
2(\tfrac{1}{2} - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### 3.2 Jacobian

Differentiating each row of $\mathbf{f}_g$ with respect to $[q_0, q_1, q_2, q_3]$:

$$
\mathbf{J}_g(\mathbf{q}) =
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & \;\;2q_1 \\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & \;\;2q_2 \\
\;\;0 & -4q_1 & -4q_2 & \;\;0
\end{bmatrix}
$$

### 3.3 Gradient

The gradient of the scalar cost $\tfrac12\|\mathbf{f}\|^2$ is:

$$
\nabla f = \mathbf{J}_g^T \mathbf{f}_g
$$

Let $f_1, f_2, f_3$ be the three rows of $\mathbf{f}_g$. The gradient components are:

$$
\nabla f =
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2 \\
\;\;2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3 \\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3 \\
\;\;2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

### 3.4 Single Gradient-Descent Step

A gradient-descent step on this cost is:

$$
\mathbf{q}_{\nabla,t} = \hat{\mathbf{q}}_{t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick's insight is that one step per sample is sufficient. The convergence rate only needs to keep pace with the physical rate of orientation change.

---

## 4. Fusion

In the original paper, the gyro estimate and the gradient-descent estimate are blended by a weight $\gamma$. Choosing $\mu_t = \alpha\|\dot{\mathbf{q}}_\omega\|\Delta t$ with large $\alpha$ makes $\gamma \approx \beta\Delta t/\mu_t$. The fused estimate then collapses into a single rate equation:

$$
\boxed{\;\dot{\mathbf{q}}_{est,t} = \dot{\mathbf{q}}_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

Interpretation:
- The gyro propagates orientation.
- The accelerometer subtracts the component of rate pointing along the error gradient.
- The correction has a fixed magnitude of $\beta$.

### 4.1 Discrete Integration and Normalization

$$
\boxed{\;\hat{\mathbf{q}}_t = \hat{\mathbf{q}}_{t-1} + \dot{\mathbf{q}}_{est,t}\,\Delta t, \qquad \hat{\mathbf{q}}_t \leftarrow \frac{\hat{\mathbf{q}}_t}{\|\hat{\mathbf{q}}_t\|}\;}
$$

---

## 5. Choosing β

$\beta$ represents the expected gyroscope measurement error expressed as a quaternion rate. If each gyro axis has zero-mean error magnitude $\tilde\omega_\beta$:

$$
\beta = \sqrt{\tfrac{3}{4}}\,\tilde\omega_\beta
$$

For example, a gyro error of 2°/s (0.035 rad/s) gives $\beta \approx 0.03$.

- **Larger β**: faster convergence, but more accelerometer noise and vibration reaches the estimate.
- **Smaller β**: smoother output, but slower correction of drift.

---

## 6. Optional: Gyro Bias Drift Compensation

Madgwick's extended version estimates gyro bias from the gradient. The gradient is converted into an angular-rate error:

$$
\boldsymbol{\omega}_{\epsilon,t} = 2\,\hat{\mathbf{q}}_{t-1}^* \otimes \frac{\nabla f}{\|\nabla f\|}
$$

The bias estimate integrates this error:

$$
\boldsymbol{\omega}_{b,t} = \zeta \sum \boldsymbol{\omega}_{\epsilon,t}\,\Delta t
$$

The bias is then removed from the raw gyro reading:

$$
\boldsymbol{\omega}_{c,t} = \boldsymbol{\omega}_t - \boldsymbol{\omega}_{b,t}
$$

Use $\boldsymbol{\omega}_{c,t}$ in Section 2. Set $\zeta = \sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the expected bias drift rate.

---

## 7