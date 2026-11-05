# Madgwick Filter: Quaternion Orientation Update (Gyro + Accelerometer IMU Variant)

I don't have context on what "Cycle 18" refers to in your project, so this is a general derivation applied to a gimbal payload IMU. Tell me your sensor rates, mounting frame or other constraints, and I can tailor it.

---

## 1. Conventions

- Orientation quaternion: $\hat{q} = {}^S_E\hat{q} = [q_0,\, q_1,\, q_2,\, q_3]^T$ (scalar first, unit norm). It describes the Earth frame *E* relative to the sensor frame *S*.
- Gyroscope measurement (rad/s), as a pure quaternion: ${}^S\omega = [0,\, \omega_x,\, \omega_y,\, \omega_z]$.
- Accelerometer measurement, normalized: ${}^S\hat{a} = [0,\, a_x,\, a_y,\, a_z]$, with $\|a\| = 1$.
- Gravity reference direction in Earth frame: ${}^E\hat{g} = [0,\, 0,\, 0,\, 1]$.
- Quaternion product $\otimes$ (Hamilton) and conjugate $\hat{q}^*$.

---

## 2. Orientation from the Gyroscope (Kinematics)

The quaternion rate of change from angular velocity is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\, {}^S_E\hat{q}_{est,t-1} \otimes {}^S\omega_t
$$

Using $(a_0,\mathbf{a})\otimes(0,\mathbf{w}) = (-\mathbf{a}\cdot\mathbf{w},\; a_0\mathbf{w} + \mathbf{a}\times\mathbf{w})$, this expands to:

$$
\begin{aligned}
\dot{q}_{\omega,0} &= \tfrac12(-q_1\omega_x - q_2\omega_y - q_3\omega_z)\\
\dot{q}_{\omega,1} &= \tfrac12(\;\;q_0\omega_x + q_2\omega_z - q_3\omega_y)\\
\dot{q}_{\omega,2} &= \tfrac12(\;\;q_0\omega_y - q_1\omega_z + q_3\omega_x)\\
\dot{q}_{\omega,3} &= \tfrac12(\;\;q_0\omega_z + q_1\omega_y - q_2\omega_x)
\end{aligned}
$$

Integrating this alone drifts, because gyro bias and noise accumulate.

---

## 3. Orientation from the Accelerometer (Optimization Problem)

We seek the orientation that rotates the Earth gravity reference into the measured sensor direction. This is posed as minimizing an objective function:

$$
\min_{\hat{q}\in\mathbb{R}^4} \tfrac12\|f(\hat{q}, {}^E\hat{d}, {}^S\hat{s})\|^2,
\qquad
f = \hat{q}^* \otimes {}^E\hat{d} \otimes \hat{q} - {}^S\hat{s}
$$

Substitute ${}^E\hat{d} = {}^E\hat{g} = [0,0,0,1]$ and ${}^S\hat{s} = {}^S\hat{a}$. The rotated gravity vector is the third column of the rotation matrix, so:

$$
f_g(\hat{q}, {}^S\hat{a}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\[2pt]
2(q_0q_1 + q_2q_3) - a_y\\[2pt]
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
\equiv
\begin{bmatrix} f_1\\ f_2\\ f_3\end{bmatrix}
$$

### Jacobian

$$
J_g(\hat{q}) = \frac{\partial f_g}{\partial \hat{q}} =
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

Expanded:

$$
\begin{aligned}
\nabla f_0 &= -2q_2 f_1 + 2q_1 f_2\\
\nabla f_1 &= \;\;2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
\nabla f_2 &= -2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\nabla f_3 &= \;\;2q_1 f_1 + 2q_2 f_2
\end{aligned}
$$

A single gradient-descent step gives the accelerometer-based estimate:

$$
{}^S_E q_{\nabla,t} = \hat{q}_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion Derivation

Madgwick combines the two estimates with a weight $\gamma_t$:

$$
q_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}
$$

The step size $\mu_t$ is chosen to be at least as fast as the physical rate of orientation change:

$$
\mu_t = \alpha\,\|\dot{q}_{\omega,t}\|\,\Delta t, \quad \alpha > 1
$$

The optimal weight balances gyro divergence rate $\beta$ against accelerometer convergence $\mu_t/\Delta t$:

$$
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

Assuming $\alpha$ is very large, $\mu_t$ becomes large, so $q_{\nabla}$ is dominated by the gradient term and $\gamma_t \approx 0$. Substituting and simplifying yields the compact filter.

---

## 5. Final Update Equations

**Rate fusion:**

$$
\boxed{\;\dot{q}_{est,t} = \dot{q}_{\omega,t} \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

**Integration and normalization:**

$$
q_{est,t} = q_{est,t-1} + \dot{q}_{est,t}\,\Delta t, \qquad
q_{est,t} \leftarrow \frac{q_{est,t}}{\|q_{est,t}\|}
$$

**Gain:**

$$
\beta = \sqrt{\tfrac{3}{4}}\;\tilde{\omega}_\beta
$$

Here $\tilde{\omega}_\beta$ is the estimated mean zero gyro measurement error per axis (rad/s). Typical values are $\beta \approx 0.03$ to $0.1$.

**Optional gyro bias compensation (Madgwick's ζ term):**

$$
\omega_{\varepsilon,t} = 2\,\hat{q}^*_{est,t-1}\otimes\frac{\nabla f}{\|\nabla f\|}, \qquad
\omega_{b,t} = \zeta\sum_t \omega_{\varepsilon,t}\Delta t, \qquad
\omega_{c,t} = \omega_t - \