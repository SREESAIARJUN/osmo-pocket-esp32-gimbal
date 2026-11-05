# Madgwick Filter: Gyroscope + Accelerometer (IMU) Orientation Update

## 0. Conventions

- Quaternion (scalar first): $\hat{q} = [q_0,\ q_1,\ q_2,\ q_3]^T = [w, x, y, z]^T$, unit norm.
- $\hat{q}$ (written ${}^S_E\hat{q}$ in Madgwick's paper) describes the orientation of the **sensor frame S** (gimbal payload IMU) relative to the **earth frame E**.
- Earth frame: z-axis up. Gravity direction: $\hat{d}_E = [0,\ 0,\ 0,\ 1]$ (pure quaternion).
- Gyro measurement: $\omega_S = [0,\ \omega_x,\ \omega_y,\ \omega_z]$ in rad/s.
- Accelerometer measurement, normalized: $\hat{a}_S = [0,\ a_x,\ a_y,\ a_z]$, with $\|\hat{a}_S\| = 1$.
- $\otimes$ is the Hamilton product and $q^*$ is the conjugate.

---

## 1. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation from the angular rate is:

$$
\dot{q}_{\omega,t} = \tfrac{1}{2}\,\hat{q}_{t-1} \otimes \omega_{S,t}
$$

Expanding the product gives:

$$
\dot{q}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z \\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y \\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x \\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone would accumulate drift from bias and noise. The accelerometer is used to correct it.

---

## 2. Accelerometer Correction as an Optimization Problem

We want the orientation $\hat{q}$ that rotates the known earth gravity direction onto the measured direction in the sensor frame:

$$
\min_{\hat q \in \mathbb{R}^4}\ \tfrac12\| f(\hat q, \hat d_E, \hat a_S)\|^2,
\qquad
f = \hat q^* \otimes \hat d_E \otimes \hat q - \hat a_S
$$

### 2.1 Objective function

With $\hat d_E = [0,0,0,1]$, the rotated gravity vector is the third row of the rotation matrix $R(\hat q)$. Dropping the zero scalar part:

$$
f_g(\hat q, \hat a_S) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x \\
2(q_0q_1 + q_2q_3) - a_y \\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### 2.2 Jacobian

Differentiate $f_g$ with respect to $[q_0, q_1, q_2, q_3]$:

$$
J_g(\hat q) =
\begin{bmatrix}
-2q_2 & \ \ 2q_3 & -2q_0 & 2q_1 \\
\ \ 2q_1 & \ \ 2q_0 & \ \ 2q_3 & 2q_2 \\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 2.3 Gradient

$$
\nabla f = J_g^T f_g
$$

Expanding this gives the form used in code, where $f_1, f_2, f_3$ are the rows of $f_g$:

$$
\nabla f =
\begin{bmatrix}
s_0\\ s_1\\ s_2\\ s_3
\end{bmatrix}
=
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2 \\
\ \ 2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3 \\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3 \\
\ \ 2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

### 2.4 Gradient-descent step

$$
\hat q_{\nabla,t} = \hat q_{t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick showed that a **single iteration per sample** is sufficient. This holds provided the step size $\mu_t$ is at least as large as the gyro-induced change, $\mu_t = \alpha\|\dot q_{\omega,t}\|\Delta t$ with $\alpha > 1$.

---

## 3. Fusing the Two Estimates

### 3.1 Weighted combination

The two estimates are combined with a weight $\gamma_t$:

$$
\hat q_t = \gamma_t\, \hat q_{\nabla,t} + (1-\gamma_t)\, \hat q_{\omega,t}, \qquad 0\le\gamma_t\le1
$$

### 3.2 Choosing the optimal weight

The optimal $\gamma_t$ balances two divergence rates:

- the gyro estimate diverges at rate $\beta$;
- the accelerometer step converges at rate $\mu_t/\Delta t$.

Setting $\gamma_t(\mu_t/\Delta t) = (1-\gamma_t)\beta$ gives $\gamma_t = \dfrac{\beta}{\mu_t/\Delta t + \beta}$.

Because $\mu_t$ is large (since $\alpha \gg 1$), $\gamma_t \approx \beta\Delta t/\mu_t \to$ small. Substituting and simplifying collapses the fusion into a rate-domain correction.

### 3.3 Final update equations

$$
\boxed{\ \dot{\hat q}_{est,t} = \tfrac12\,\hat q_{t-1}\otimes\omega_{S,t} \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\ }
$$

$$
\boxed{\ \hat q_t = \hat q_{t-1} + \dot{\hat q}_{est,t}\,\Delta t, \qquad \hat q_t \leftarrow \frac{\hat q_t}{\|\hat q_t\|}\ }
$$

---

## 4. Choosing the Gain β

$\beta$ represents the gyro measurement error expressed as a quaternion rate. Assume an equal error $\tilde\omega_\beta$ (rad/s) on each axis. Then:

$$
\left\|\tfrac12\,\hat q\otimes[0,\tilde\omega_\beta,\tilde\omega_\beta,\tilde\omega_\beta]\right\| = \tfrac12\sqrt{3}\,\tilde\omega_\beta
\quad\Rightarrow\quad
\boxed{\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta}
$$

The choice of $\beta$ is a trade-off:

- **Larger β:** faster convergence, but more accelerometer noise and vibration leaks into the estimate.
- **Smaller β:** smoother output, but more drift.

Typical values are 0.03–0.1.

### Optional: gyro bias compensation (ζ)

The gyro bias can be estimated from the correction term and subtracted from the gyro reading:

$$
\omega_{\epsilon,t} = 2\,\hat q_{t-1}^*\otimes \frac{\nabla f}{\|\nabla f\|},\qquad
\omega_{b,t} = \zeta\sum \omega_{\epsilon,t}\Delta t,\qquad
\omega_{c,t} = \omega_{S,t} - \omega_{b,t}
$$

Use $\omega_{c,t}$ in place of $\omega_{S,t}$ in Section 1. Here $\zeta = \sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the gyro bias drift rate.

---

## 5. Gimbal-Payload