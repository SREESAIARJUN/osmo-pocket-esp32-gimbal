# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

**Context:** gimbal payload IMU, design Cycle 22

---

## 1. Conventions

- **Quaternion:** $\hat{q} = {}^S_E\hat{q} = [q_0\; q_1\; q_2\; q_3]^T$ (scalar first). It describes the orientation of the Earth frame $E$ relative to the sensor (payload) frame $S$.
- **Quaternion product:** for $a \otimes b$,

$$
a\otimes b=\begin{bmatrix}
a_0b_0-a_1b_1-a_2b_2-a_3b_3\\
a_0b_1+a_1b_0+a_2b_3-a_3b_2\\
a_0b_2-a_1b_3+a_2b_0+a_3b_1\\
a_0b_3+a_1b_2-a_2b_1+a_3b_0
\end{bmatrix}
$$

- **Conjugate:** $\hat{q}^* = [q_0\; -q_1\; -q_2\; -q_3]^T$.
- **Measurements:**
  - Gyroscope: ${}^S\omega = [0\;\omega_x\;\omega_y\;\omega_z]$ in rad/s.
  - Accelerometer: ${}^S\hat{a} = [0\;a_x\;a_y\;a_z]$, normalized to unit length.

---

## 2. Orientation from the Gyroscope (Kinematics)

The quaternion derivative for a body rotating at ${}^S\omega$ is:

$$
\dot{q}_{\omega,t} = \tfrac12\,\hat{q}_{est,t-1}\otimes {}^S\omega_t
$$

Expanded:

$$
\dot{q}_\omega=\tfrac12\begin{bmatrix}
-q_1\omega_x-q_2\omega_y-q_3\omega_z\\
\;\;q_0\omega_x+q_2\omega_z-q_3\omega_y\\
\;\;q_0\omega_y-q_1\omega_z+q_3\omega_x\\
\;\;q_0\omega_z+q_1\omega_y-q_2\omega_x
\end{bmatrix}
$$

Integrating this alone gives an accurate short-term solution. However, gyro bias and noise make it drift without bound.

---

## 3. Orientation from the Accelerometer (Optimization Problem)

The accelerometer measures the gravity direction in the sensor frame. We seek the orientation $\hat q$ that rotates the known Earth reference direction ${}^E\hat d$ into the measured direction ${}^S\hat s$:

$$
\min_{\hat q}\; f(\hat q, {}^E\hat d, {}^S\hat s),\qquad
f = \hat q^*\otimes{}^E\hat d\otimes\hat q - {}^S\hat s
$$

With gravity ${}^E\hat g = [0\;0\;0\;1]$ and ${}^S\hat s = {}^S\hat a$, the objective becomes:

$$
f_g(\hat q,{}^S\hat a)=\begin{bmatrix}
2(q_1q_3-q_0q_2)-a_x\\
2(q_0q_1+q_2q_3)-a_y\\
2(\tfrac12-q_1^2-q_2^2)-a_z
\end{bmatrix}
$$

Its Jacobian is:

$$
J_g(\hat q)=\begin{bmatrix}
-2q_2 & 2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & 2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

The gradient of the cost $\tfrac12\|f_g\|^2$ is:

$$
\nabla f = J_g^T\, f_g
$$

Expanded (as used in efficient code):

$$
\nabla f=\begin{bmatrix}
4q_0q_2^2+2q_2a_x+4q_0q_1^2-2q_1a_y\\
4q_1q_3^2-2q_3a_x+4q_0^2q_1-2q_0a_y-4q_1+8q_1^3+8q_1q_2^2+4q_1a_z\\
4q_0^2q_2+2q_0a_x+4q_2q_3^2-2q_3a_y-4q_2+8q_1^2q_2+8q_2^3+4q_2a_z\\
4q_1^2q_3-2q_1a_x+4q_2^2q_3-2q_2a_y
\end{bmatrix}
$$

*(Check of row 0: $-2q_2 f_1 + 2q_1 f_2$; the $q_1q_2q_3$ terms cancel.)*

A single gradient-descent step gives the accelerometer-based estimate:

$$
q_{\nabla,t} = \hat q_{est,t-1} - \mu_t\,\frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion

Madgwick blends the two estimates with a weight $\gamma_t$:

$$
q_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}
$$

The optimal $\gamma_t$ makes the convergence rate of $q_\nabla$ equal to the gyro divergence rate. The step size is chosen as $\mu_t = \alpha\|\dot q_\omega\|\Delta t$ with $\alpha \gg 1$. This yields:

$$
\gamma_t = \frac{\beta\,\Delta t}{\mu_t/\Delta t + \beta}
$$

Because $\mu_t$ is large, $\gamma_t \approx 0$. Substituting and simplifying gives the **error-rate form**:

$$
\boxed{\dot q_{est,t} = \dot q_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}}
$$

$$
\boxed{\hat q_{est,t} = \frac{\hat q_{est,t-1} + \dot q_{est,t}\,\Delta t}{\left\|\hat q_{est,t-1} + \dot q_{est,t}\,\Delta t\right\|}}
$$

### Gain β

$\beta$ represents the gyro measurement error expressed as a quaternion rate:

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

Here $\tilde\omega_\beta$ is the expected per-axis gyro error in rad/s. For example, 2 °/s gives $\beta \approx 0.030$.

### Optional gyro-bias compensation

$$
\omega_{\epsilon} = 2\,\hat q^*_{est,t-1}\otimes \frac{\nabla f}{\|\nabla f\|},\qquad
\omega_{b,t} = \zeta\sum \omega_{\epsilon}\Delta t,\qquad
{}^S\omega_c = {}^S\omega - \omega_b
$$

with $\zeta = \sqrt{\tfrac34}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the expected bias drift rate.

---

## 5. Discrete Algorithm (per sample)

```
qDot = 0.5 * q ⊗ [0, gx, gy, gz]
if ‖a‖ > 0:
    a  = a / ‖a‖
    s  = J_gᵀ(q) · f_g(q, a)          # gradient, Section 3
    s  = s / ‖s‖
    qDot -= beta * s
q = q + qDot * dt
q = q / ‖q‖
```

---

## 6. Gimbal-Payload Considerations

1. **Non-gravitational acceleration.** The payload accelerometer also sees vehicle acceleration and lever-arm terms ($\dot\omega\times r$, $\omega\times\omega\times r$). Mitigations: