# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

*Note: I don't have the context for "Cycle 12" (a project phase, design review, or document revision on your side). The derivation below is the standard IMU-only Madgwick formulation, with notes specific to a gimbal payload. Map the notation onto your cycle documentation as needed.*

---

## 1. Notation and Conventions

- Quaternion ${}^S_E\hat{q} = [q_0,\ q_1,\ q_2,\ q_3]^T$, with $q_0$ the scalar part. It describes the orientation of the Earth frame $E$ relative to the sensor (payload) frame $S$, and is kept at unit norm.
- $\otimes$ is the Hamilton quaternion product, and $\hat{q}^*$ is the conjugate.
- Gyroscope measurement: ${}^S\omega = [0,\ \omega_x,\ \omega_y,\ \omega_z]$ in rad/s.
- Accelerometer measurement, normalized: ${}^S\hat{a} = [0,\ a_x,\ a_y,\ a_z]$.
- Earth gravity reference, normalized: ${}^E\hat{g} = [0,\ 0,\ 0,\ 1]$.

---

## 2. Gyroscope Path: Quaternion Kinematics

The rate of change of orientation from the angular rate is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\,{}^S_E\hat{q}_{est,t-1} \otimes {}^S\omega_t
$$

Expanded:

$$
\dot{q}_\omega = \tfrac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone drifts, because gyro bias and noise accumulate.

---

## 3. Accelerometer Path: Orientation as an Optimization Problem

### 3.1 Objective function

We look for the orientation that rotates the Earth gravity reference into the measured sensor-frame acceleration:

$$
\min_{q}\ \tfrac12\|f(q,\,{}^E\hat{g},\,{}^S\hat{a})\|^2,
\qquad
f = \hat{q}^* \otimes {}^E\hat{g} \otimes \hat{q} - {}^S\hat{a}
$$

### 3.2 Substituting $g = [0,0,0,1]$

$$
f_g(q,{}^S\hat a)=
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### 3.3 Jacobian

Differentiating each row with respect to $[q_0, q_1, q_2, q_3]$:

$$
J_g(q)=
\begin{bmatrix}
-2q_2 & \ \ 2q_3 & -2q_0 & 2q_1\\
\ \ 2q_1 & \ \ 2q_0 & \ \ 2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 3.4 Gradient

$$
\nabla f = J_g^T(q)\, f_g(q,{}^S\hat a)
$$

Expanded, and simplified using $\|q\|=1$, the gradient components are:

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

For example, $s_0 = -2q_2 f_1 + 2q_1 f_2$. The cross terms $\pm 4q_1q_2q_3$ cancel.

### 3.5 Gradient-descent step

A single gradient-descent step per sample is sufficient, provided the step size is at least as fast as the physical rate of orientation change:

$$
q_{\nabla,t} = \hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion

Madgwick combines the two estimates as a weighted sum, $q_{est} = \gamma q_\nabla + (1-\gamma) q_\omega$. He then chooses $\gamma$ so that the gyro divergence rate balances the accelerometer convergence rate, with $\mu_t = \alpha\|\dot q_\omega\|\Delta t$ and $\alpha \gg 1$. This reduces the filter to a single rate equation:

$$
\boxed{\ {}^S_E\dot{q}_{est,t} = \tfrac12\,\hat q_{est,t-1}\otimes{}^S\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\ }
$$

**Integration and normalization:**

$$
q_{est,t} = q_{est,t-1} + \dot q_{est,t}\,\Delta t,
\qquad
\hat q_{est,t} = \frac{q_{est,t}}{\|q_{est,t}\|}
$$

### 4.1 Choosing β

β is the magnitude of the gyro measurement error, expressed as a quaternion derivative. If $\tilde\omega_\beta$ is the estimated mean zero-mean gyro error on each axis:

$$
\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta
$$

- **Larger β:** faster convergence, but more accelerometer noise is admitted.
- **Smaller β:** smoother output, but more gyro drift.

### 4.2 Optional gyro bias compensation

The MARG version of the filter adds bias compensation:

$$
\omega_{b,t} = \zeta\!\int 2\,\hat q^*_{est}\otimes \hat{\dot q}_\epsilon\,dt,
\qquad
\omega_c = \omega - \omega_b,
\qquad
\zeta = \sqrt{\tfrac34}\,\tilde{\dot\omega}_\zeta
$$

---

## 5. Algorithm per Sample

1. Read the gyro rates $\omega$ and the accelerometer vector $a$.
2. Compute $\dot q_\omega = \tfrac12 q\otimes[0,\omega]$.
3. If $\|a\|\neq 0$:
   - Normalize $a$.
   - Compute $s_0 \dots s_3$.
   - Normalize $s$.
   - Update $\dot q \mathrel{-}= \beta s$.
4. Integrate: $q \mathrel{