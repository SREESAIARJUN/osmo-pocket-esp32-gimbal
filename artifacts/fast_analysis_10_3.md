# Madgwick Filter Derivation: Gyroscope and Accelerometer (IMU) Form

## 0. Conventions

- **Quaternion:** $\hat q = [q_0\; q_1\; q_2\; q_3]^T$, with $q_0$ as the scalar part and $\|\hat q\| = 1$.
- **Frame direction:** $\hat q = {}^S_E\hat q$ describes the orientation of the Earth frame $E$ relative to the sensor (payload) frame $S$.
- **Rotating a vector:** a vector $v$ written as a pure quaternion $[0, v]$ is rotated by
$$ {}^S v = \hat q^* \otimes {}^E v \otimes \hat q $$
- **Sensor inputs:** gyro rates ${}^S\omega = [0,\ \omega_x,\ \omega_y,\ \omega_z]$ in rad/s, and accelerometer ${}^S a = [0,\ a_x,\ a_y,\ a_z]$, normalized to unit length.

---

## 1. Gyroscope Propagation (Kinematics)

The quaternion rate of change driven by the measured angular rate is:

$$
\dot q_{\omega,t} = \tfrac12\, \hat q_{t-1} \otimes {}^S\omega_t
$$

Expanding the quaternion product gives:

$$
\dot q_\omega = \tfrac12
\begin{bmatrix}
- q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;\, q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;\, q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;\, q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone would drift without bound because of gyro bias and noise. The accelerometer supplies the correction.

---

## 2. Accelerometer Correction as an Optimization Problem

Find the $\hat q$ that best rotates the known Earth gravity direction ${}^E g = [0,0,0,1]$ onto the measured direction ${}^S \hat a$. Define the objective:

$$
\min_{\hat q}\; \tfrac12\|f(\hat q, {}^E g, {}^S\hat a)\|^2,\qquad
f = \hat q^* \otimes {}^E g \otimes \hat q - {}^S\hat a
$$

Substituting ${}^E g = [0,0,0,1]$, the rotated gravity vector is the third row of the rotation matrix. Therefore:

$$
f_g(\hat q, \hat a) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### Jacobian

Differentiate each component of $f_g$ with respect to $q_0, q_1, q_2, q_3$:

$$
J_g(\hat q) =
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### Gradient

$$
\nabla f = J_g^T f_g
$$

Expanding and simplifying with $\|\hat q\| = 1$ gives the form used in Madgwick's reference code:

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

### Gradient-Descent Step

A gradient-descent step is $\hat q_{k+1} = \hat q_k - \mu \nabla f/\|\nabla f\|$. Madgwick's key insight is that **one iteration per sample is sufficient**, provided the step size keeps pace with the true rotation rate. The step is therefore treated as a corrective rate:

$$
\dot q_{\epsilon} = \frac{\nabla f}{\|\nabla f\|}
$$

---

## 3. Fusion: The Madgwick Update Law

Subtract the normalized correction, scaled by $\beta$, from the gyro rate:

$$
\boxed{\;\dot{\hat q}_{est,t} = \tfrac12\,\hat q_{t-1}\otimes{}^S\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

Then integrate and renormalize:

$$
\boxed{\;\hat q_t = \hat q_{t-1} + \dot{\hat q}_{est,t}\,\Delta t,\qquad \hat q_t \leftarrow \frac{\hat q_t}{\|\hat q_t\|}\;}
$$

The two terms play opposite roles:
- **Gyro term:** dominates at high frequency.
- **Accelerometer gradient term:** pulls roll and pitch toward gravity at low frequency.

Together they act as a nonlinear complementary filter.

---

## 4. Choosing the Gain β

$\beta$ is set equal to the magnitude of quaternion-rate error caused by gyro measurement error. Assume an error of $\tilde\omega_\beta$ (rad/s) on each axis:

$$
\beta = \left\|\tfrac12\,\hat q \otimes [0,\ \tilde\omega_\beta,\ \tilde\omega_\beta,\ \tilde\omega_\beta]\right\|
= \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

For example, a gyro error of 3°/s gives $\beta \approx 0.866 \times 0.0524 \approx 0.045$.

### Optional Gyro-Bias Compensation

The bias can also be estimated by integrating the correction term:

$$
\omega_{b,t} = \zeta \sum 2\,\hat q^*\otimes\dot q_\epsilon\,\Delta t, \qquad \omega_c = \omega - \omega_b, \qquad \zeta = \sqrt{\tfrac34}\,\dot{\tilde\omega}_\zeta
$$

where $\dot{\tilde\omega}_\zeta$ is the expected gyro bias drift rate.

---

## 5. Per-Cycle Algorithm (Gimbal Payload Loop)

```
read ω (rad/s), a
qDot = 0.5 * q ⊗ [0, ω]
if |a| != 0:
    a = a / |a|
    s = J_g(q)^T * f_g(q, a)        # s0..s3 above
    s = s / |s|
    qDot -= β * s
q += qDot * dt
q /= |q|
```

---

## 6. Payload-Specific Considerations

- **Yaw is unobservable.** Gravity constrains only roll and pitch, so yaw drifts at the residual gyro bias rate. Add a magnetometer (MARG form, with a second objective $f_b$ and stacked Jacobian) or use the gimbal encoders or the vehicle heading for yaw.
- **Linear acceleration corrupts the gravity reference.**