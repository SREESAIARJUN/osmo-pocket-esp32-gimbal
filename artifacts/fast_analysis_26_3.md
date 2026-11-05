# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU) Orientation Update

*I don't know what "Cycle 26" refers to in your project, so I've treated it as a label. The derivation below is the standard 6-DOF Madgwick formulation, with notes for an IMU mounted on a gimbal payload.*

---

## 1. Conventions

- **Quaternion:** ${}^S_E\hat{q} = [q_1\; q_2\; q_3\; q_4]^T$, where $q_1$ is the scalar part. It describes the orientation of the Earth frame $E$ relative to the sensor frame $S$, i.e. the payload IMU frame.
- **Gyroscope:** ${}^S\omega = [0\; \omega_x\; \omega_y\; \omega_z]$, in rad/s.
- **Accelerometer:** ${}^S\hat{a} = [0\; a_x\; a_y\; a_z]$, normalized to unit length.
- **Gravity reference in the Earth frame:** ${}^E\hat{g} = [0\; 0\; 0\; 1]$.
- **Quaternion product:** $\otimes$ denotes the Hamilton product.
- **Conjugate:** $\hat{q}^*$ denotes the quaternion conjugate.

---

## 2. Orientation from the Gyroscope (Quaternion Kinematics)

The rate of change of orientation follows from the angular rate:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\, {}^S_E\hat{q}_{est,t-1} \otimes {}^S\omega_t
$$

Expanded:

$$
\dot{q}_\omega = \tfrac{1}{2}
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\;\;q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\;\;q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\;\;q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

Integrating this alone accumulates gyro bias and noise, so the attitude drifts without bound. The accelerometer supplies the correction.

---

## 3. Orientation from the Accelerometer (Optimization Problem)

We seek the orientation that rotates the known Earth gravity direction onto the measured sensor direction. Define the objective function:

$$
f(\hat{q}, {}^E\hat{d}, {}^S\hat{s}) = \hat{q}^* \otimes {}^E\hat{d} \otimes \hat{q} - {}^S\hat{s}
$$

$$
\min_{\hat q \in \mathbb{R}^4} \; \tfrac12 \| f \|^2
$$

Substitute ${}^E\hat{d} = {}^E\hat{g} = [0,0,0,1]$ and ${}^S\hat{s} = {}^S\hat{a}$. The rotated gravity vector equals the third column of the rotation matrix, which gives:

$$
f_g(\hat q, {}^S\hat a) =
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

### Jacobian

Differentiating $f_g$ with respect to $[q_1, q_2, q_3, q_4]$:

$$
J_g(\hat q) =
\begin{bmatrix}
-2q_3 & 2q_4 & -2q_1 & 2q_2\\
\;\;2q_2 & 2q_1 & \;\;2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### Gradient

$$
\nabla f = J_g^T f_g
$$

Writing $f_g = [f_1, f_2, f_3]^T$:

$$
\nabla f =
\begin{bmatrix}
-2q_3 f_1 + 2q_2 f_2\\
\;\;2q_4 f_1 + 2q_1 f_2 - 4q_2 f_3\\
-2q_1 f_1 + 2q_4 f_2 - 4q_3 f_3\\
\;\;2q_2 f_1 + 2q_3 f_2
\end{bmatrix}
$$

### One Gradient-Descent Step

$$
q_{\nabla,t} = \hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Only one iteration is needed per sample, provided the step size $\mu_t$ keeps pace with the true rotation rate. The step size is chosen as

$$
\mu_t = \alpha \,\|\dot q_{\omega,t}\|\,\Delta t, \qquad \alpha > 1.
$$

---

## 4. Fusion

Fuse the two estimates with a weight $\gamma_t$:

$$
q_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}
$$

The optimal weighting balances the gyro's divergence rate against the convergence rate of the gradient step, $\mu_t/\Delta t$. This gives

$$
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

When $\alpha$ is very large (equivalently, $\mu_t$ is large), the weight becomes $\gamma_t \approx \beta\Delta t/\mu_t \to 0$. Substituting and simplifying collapses the fusion into a single rate equation, the **Madgwick update law**:

$$
\boxed{\;\dot q_{est,t} = \dot q_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

$$
\boxed{\;\hat q_{est,t} = \frac{\hat q_{est,t-1} + \dot q_{est,t}\,\Delta t}{\left\|\hat q_{est,t-1} + \dot q_{est,t}\,\Delta t\right\|}\;}
$$

### Gain β

$\beta$ represents the gyro measurement error expressed as a quaternion rate. For a gyro error of $\tilde\omega_\beta$ (rad/s) on each axis:

$$
\beta = \sqrt{\tfrac{3}{4}}\;\tilde\omega_\beta
$$

---

## 5. Optional: Gyro Bias Compensation

The normalized gradient direction approximates the orientation-rate error. Its implied angular-rate error is

$$
\omega_{\epsilon,t} = 2\,\hat q_{est,t-1}^* \otimes \frac{\nabla f}{\|\nabla f\|}
$$

Accumulate the bias estimate and subtract it from the gyro reading:

$$
\omega_{b,t} = \zeta \sum_t \omega_{\epsilon,t}\,\Delta t, \qquad \omega_{c,t} = \omega_t - \omega_{b,t}
$$

Here $\zeta = \sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the gyro bias drift rate. Use $\omega_c$ in place of $\omega$ in Section 2.

---

## 6. Per-Sample Algorithm

1. Read $\omega$ and $a$. If $\|a\| \neq 0$, normalize $a$.
2. Compute $\dot q_\omega = \tfrac12 \hat q \otimes [0,\omega]$.
3. Compute $f_g$, then $\nabla f = J_g^T f_g$, and normalize $\nabla f$.
4. Compute $\dot q = \dot q_\omega - \beta\,\widehat{\nabla f}$.
5. Integrate $q \leftarrow q + \dot q\,\Delta t$, then normalize $q$.

---

## 7. Gimbal-Payload Considerations

- **Yaw is unobservable.** Gravity constrains only roll and pitch, so the IMU-only filter drifts in heading. Add a magnetometer (MARG variant) or use the gimbal encoders and the host vehicle heading for yaw.
- **Linear acceleration corrupts the gravity reference.** Slews and vehicle maneuvers add acceleration that the filter misreads as tilt. Gate or reduce $\beta$ when $\big|\|a\|_{raw} - g\big| > \text{threshold}$, or subtract the known centripetal and tangential terms using the encoder rates and the IM