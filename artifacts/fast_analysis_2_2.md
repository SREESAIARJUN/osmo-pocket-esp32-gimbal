# Madgwick Filter Derivation: Gyro + Accelerometer Fusion for a Gimbal Payload

> **Note on "Cycle 2":** I don't have the context of your earlier cycle, so this derivation is self-contained. It assumes a 6-DOF IMU (3-axis gyroscope and 3-axis accelerometer, no magnetometer) mounted on the gimbal payload. If Cycle 1 fixed different frame conventions or sensor axes, map them onto the conventions defined in Section 0.

---

## 0. Conventions

- Quaternion: $\mathbf{q} = [q_0,\; q_1,\; q_2,\; q_3]^T$, with $q_0$ the scalar part and $\|\mathbf{q}\| = 1$.
- $\mathbf{q} \equiv {}^{S}_{E}\hat{\mathbf{q}}$ describes the orientation of the **Earth frame E relative to the sensor (payload) frame S**, following Madgwick's convention.
- Quaternion product $\otimes$ (Hamilton). Conjugate: $\mathbf{q}^* = [q_0, -q_1, -q_2, -q_3]^T$.
- Vectors are embedded as pure quaternions: $\mathbf{v} \to [0,\, v_x,\, v_y,\, v_z]$.
- Gyroscope measurement: ${}^S\boldsymbol{\omega} = [0,\, \omega_x,\, \omega_y,\, \omega_z]$ in rad/s.
- Accelerometer measurement, normalized: ${}^S\hat{\mathbf{a}} = [0,\, a_x,\, a_y,\, a_z]$.

---

## 1. Gyroscope Path: Quaternion Kinematics

The rate of change of the orientation quaternion due to body angular rate is

$$
\dot{\mathbf{q}}_{\omega} = \tfrac{1}{2}\, \mathbf{q} \otimes {}^S\boldsymbol{\omega}
$$

Expanding the product gives

$$
\dot{\mathbf{q}}_{\omega} = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z \\
\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y \\
\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x \\
\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone gives a good short-term attitude estimate. Over time it drifts because of gyro bias and noise.

---

## 2. Accelerometer Path: Orientation as an Optimization Problem

### 2.1 Objective function

When the payload is quasi-static, the accelerometer measures only gravity. Let the Earth-frame gravity direction be

$$
{}^E\hat{\mathbf{g}} = [0,\, 0,\, 0,\, 1]
$$

The orientation $\mathbf{q}$ should rotate this reference vector into the measured sensor-frame vector. The residual to minimize is

$$
\mathbf{f}(\mathbf{q}, {}^E\hat{\mathbf{g}}, {}^S\hat{\mathbf{a}}) = \mathbf{q}^* \otimes {}^E\hat{\mathbf{g}} \otimes \mathbf{q} \;-\; {}^S\hat{\mathbf{a}}
$$

### 2.2 Expanded residual

Rotating $[0,0,1]$ by $\mathbf{q}$ gives the third column of the rotation matrix:

$$
\mathbf{f}_g(\mathbf{q}, {}^S\hat{\mathbf{a}}) =
\begin{bmatrix}
2(q_1 q_3 - q_0 q_2) - a_x \\
2(q_0 q_1 + q_2 q_3) - a_y \\
2\left(\tfrac{1}{2} - q_1^2 - q_2^2\right) - a_z
\end{bmatrix}
=
\begin{bmatrix} f_1 \\ f_2 \\ f_3 \end{bmatrix}
$$

### 2.3 Jacobian

Differentiate each residual row with respect to $(q_0, q_1, q_2, q_3)$:

$$
\mathbf{J}_g(\mathbf{q}) = \frac{\partial \mathbf{f}_g}{\partial \mathbf{q}} =
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & \;\;2q_1 \\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & \;\;2q_2 \\
\;\;0 & -4q_1 & -4q_2 & \;\;0
\end{bmatrix}
$$

### 2.4 Gradient of the cost

The cost is $\tfrac{1}{2}\|\mathbf{f}_g\|^2$. Its gradient is

$$
\nabla f = \mathbf{J}_g^T(\mathbf{q})\, \mathbf{f}_g(\mathbf{q}, {}^S\hat{\mathbf{a}})
$$

Explicitly, $\nabla f = [s_0, s_1, s_2, s_3]^T$ with

$$
\begin{aligned}
s_0 &= -2q_2 f_1 + 2q_1 f_2 \\
s_1 &= \;\;\,2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3 \\
s_2 &= -2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3 \\
s_3 &= \;\;\,2q_1 f_1 + 2q_2 f_2
\end{aligned}
$$

### 2.5 Single gradient-descent step

A full optimization at each sample would be expensive. Madgwick takes **one normalized gradient step per sample**:

$$
\dot{\mathbf{q}}_{\nabla} = -\beta \frac{\nabla f}{\|\nabla f\|}
$$

Normalizing the gradient makes the correction a fixed-magnitude rate, $\beta$. This rate has the same units as $\dot{\mathbf{q}}$, which lets it compete directly with the gyro term.

---

## 3. Fusion: Combined Update Equations

Madgwick showed that if the gradient step is tuned to the gyro's convergence rate, the complementary weighting reduces to a simple sum. The derivative contributions add:

$$
\boxed{\;\dot{\mathbf{q}}_{est,t} = \tfrac{1}{2}\,\hat{\mathbf{q}}_{est,t-1} \otimes {}^S\boldsymbol{\omega}_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

### Integration and normalization

$$
\mathbf{q}_{est,t} = \hat{\mathbf{q}}_{est,t-1} + \dot{\mathbf{q}}_{est,t}\,\Delta t
$$

$$
\boxed{\;\hat{\mathbf{q}}_{est,t} = \frac{\mathbf{q}_{est,t}}{\|\mathbf{q}_{est,t}\|}\;}
$$

### The gain β

β is set from the expected gyro measurement error $\tilde{\omega}_\beta$ (rad/s, per axis). Each axis error contributes to the quaternion derivative, giving

$$
\beta = \sqrt{\tfrac{3}{4}}\;\tilde{\omega}_\beta
$$

- **Larger β:** faster convergence and less drift, but more accelerometer noise and more sensitivity to motion.
- **Smaller β:** smoother output, but slower correction of drift.

---

## 4. Optional: Gyro Bias Compensation

For a gimbal, gyro bias is the dominant source of slow drift. Madgwick's extension estimates bias from the gradient direction.

1. Convert the normalized gradient into an angular-error rate:

$$
\boldsymbol{\omega}_{\epsilon} = 2\,\hat{\mathbf{q}}^*_{est,t-1} \otimes \frac{\nabla f}{\|\nabla f\|}
$$

2. Integrate the bias estimate:

$$
\boldsymbol{\omega}_{b,t} = \boldsymbol{\omega}_{b,t-1} + \zeta\,\boldsymbol{\omega}_{\epsilon}\,\Delta t
$$

3. Correct the gyro before the kinematic update:

$$
{}^S\boldsymbol{\omega}_{c} = {}^S\boldsymbol{\omega} - \boldsymbol{\omega}_{b}
$$

The bias gain is set from the expected bias drift rate $\tilde{\dot{\omega}}_\zeta$