# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Form)

I don't have the specifics of your "Cycle 17" payload, such as sensor part numbers, mounting frame or loop rate. The derivation below is the general 6-DOF form. Gimbal-specific notes are at the end.

---

## 1. Notation and Conventions

- Quaternion $\hat{q} = {}^S_E\hat{q} = [q_0\ q_1\ q_2\ q_3]^T$ (scalar first). It describes the orientation of the Earth frame $E$ relative to the sensor frame $S$, and it has unit norm.
- Quaternion product: $\otimes$.
- Conjugate: $\hat{q}^* = [q_0\ {-q_1}\ {-q_2}\ {-q_3}]^T$.
- Gyroscope measurement (rad/s), written as a pure quaternion: ${}^S\omega = [0\ \omega_x\ \omega_y\ \omega_z]^T$.
- Accelerometer measurement, normalized: ${}^S\hat{a} = [0\ a_x\ a_y\ a_z]^T$, with $\|a\| = 1$.
- Earth gravity reference direction: ${}^E\hat{g} = [0\ 0\ 0\ 1]^T$.

---

## 2. Gyroscope Path: Quaternion Kinematics

The rate of change of orientation from angular rate is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\,{}^S_E\hat{q}_{est,t-1} \otimes {}^S\omega_t
$$

Expanding the product gives:

$$
\dot{q}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone, $q_{\omega,t} = q_{est,t-1} + \dot q_{\omega,t}\Delta t$, drifts without bound because of gyro bias and noise.

---

## 3. Accelerometer Path: Orientation as an Optimization Problem

We seek the orientation $\hat q$ that rotates the known Earth reference ${}^E\hat d$ into the measured sensor-frame direction ${}^S\hat s$:

$$
\min_{\hat q \in \mathbb{R}^4} \; \tfrac12\|f(\hat q, {}^E\hat d, {}^S\hat s)\|^2,
\qquad
f = \hat q^* \otimes {}^E\hat d \otimes \hat q - {}^S\hat s
$$

### 3.1 Objective Function for Gravity

Substitute ${}^E\hat d = \hat g = [0\,0\,0\,1]$ and ${}^S\hat s = \hat a$. Rotating $\hat g$ into the sensor frame gives the third column of the rotation matrix, so:

$$
f_g(\hat q, \hat a) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
\equiv
\begin{bmatrix} f_1\\ f_2\\ f_3\end{bmatrix}
$$

### 3.2 Jacobian

$$
J_g(\hat q) = \frac{\partial f_g}{\partial \hat q} =
\begin{bmatrix}
-2q_2 & \ \ 2q_3 & -2q_0 & \ \ 2q_1\\
\ \ 2q_1 & \ \ 2q_0 & \ \ 2q_3 & \ \ 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 3.3 Gradient

The gradient of the cost $\tfrac12\|f\|^2$ is $\nabla f = J^T f$:

$$
\nabla f = J_g^T f_g =
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2\\
\ \ 2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\ \ 2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

### 3.4 Gradient-Descent Step

One gradient-descent step from the previous estimate is:

$$
q_{\nabla,t} = \hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Note that gravity constrains only roll and pitch. Rotation about the gravity vector (yaw) leaves $f_g$ unchanged. This is why the gradient's projection onto yaw is zero.

---

## 4. Fusion

Combine the two estimates by convex weighting:

$$
q_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}, \qquad 0 \le \gamma_t \le 1
$$

**Choosing the weight.** The optimal $\gamma_t$ balances two rates:

- the rate at which the gradient step removes error, $\gamma_t\,\mu_t/\Delta t$;
- the rate at which gyro error accumulates, $(1-\gamma_t)\,\beta$.

Setting them equal gives:

$$
(1-\gamma_t)\beta = \gamma_t \frac{\mu_t}{\Delta t}
\;\Rightarrow\;
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

To guarantee convergence, choose $\mu_t = \alpha\|\dot q_\omega\|\Delta t$ with $\alpha \gg 1$. Then $\mu_t$ is large, so $\gamma_t \approx \beta\Delta t/\mu_t \approx 0$. Substituting this back:

$$
q_{est,t} \approx \frac{\beta\Delta t}{\mu_t}\Big(q_{est,t-1} - \mu_t\tfrac{\nabla f}{\|\nabla f\|}\Big) + q_{\omega,t}
$$

The first term $\frac{\beta\Delta t}{\mu_t}q_{est,t-1}$ vanishes because $\mu_t$ is large. Also, $q_{\omega,t} = q_{est,t-1} + \dot q_\omega \Delta t$. This yields the final update.

---

## 5. Final Update Equations

$$
\boxed{\;\dot q_{est,t} = \tfrac12\, \hat q_{est,t-1}\otimes{}^S\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

$$
\boxed{\;q_{est,t} = \hat q_{est,t-1} + \dot q_{est,t}\,\Delta t, \qquad \hat q_{est,t} = \frac{q_{est,t}}{\|q_{est,t}\|}\;}
$$

### 5.1 Choosing β (Filter Gain)

Let $\tilde\omega_\beta$ be the expected gyroscope measurement error per axis (rad/s), with the worst case being equal error on all three axes. The resulting error in $\dot q$ has magnitude:

$$
\big\|\tfrac12 \hat q \otimes [0\ \tilde\omega_\beta\ \tilde\omega_\beta\ \tilde\omega_\beta]\big\|
= \tfrac12\sqrt3\,\tilde\omega_\beta
$$

Therefore:

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

The effect of tuning β:

- **Larger β:** faster correction, but more accelerometer noise and linear acceleration leaks into the estimate.
- **Smaller β:** smoother, but slower to