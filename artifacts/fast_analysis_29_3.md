# Madgwick Filter: Quaternion Orientation Update from Gyroscope and Accelerometer

This derivation targets the 6-axis (IMU) variant: a 3-axis gyroscope and a 3-axis accelerometer mounted on the gimbal payload, with no magnetometer. "Cycle 29" is not referenced below, so it is not used.

---

## 1. Conventions

- The orientation quaternion $\hat{q} = {}^S_E\hat{q} = [q_1\ q_2\ q_3\ q_4]^T$ maps the Earth frame E into the sensor/payload frame S. The scalar part is $q_1$.
- $\otimes$ is the Hamilton product, and $\hat{q}^*$ is the conjugate.
- Gyroscope measurement: ${}^S\omega = [0\ \omega_x\ \omega_y\ \omega_z]$ in rad/s.
- Accelerometer measurement: ${}^S\hat{a} = [0\ a_x\ a_y\ a_z]$, normalized to unit length.
- Gravity reference in the Earth frame: ${}^E\hat{g} = [0\ 0\ 0\ 1]$.

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of the Earth-to-sensor orientation is:

$$
{}^S_E\dot{q}_{\omega} = \tfrac{1}{2}\,{}^S_E\hat{q} \otimes {}^S\omega
$$

Integrating at discrete time $t$ with period $\Delta t$:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\,\hat{q}_{est,t-1}\otimes{}^S\omega_t,
\qquad
q_{\omega,t} = \hat{q}_{est,t-1} + {}^S_E\dot{q}_{\omega,t}\,\Delta t
$$

Written out explicitly:

$$
\dot{q}_\omega = \tfrac12
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\ \ q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\ \ q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\ \ q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

This prediction alone drifts, because gyro bias and noise integrate over time.

---

## 3. Accelerometer Correction as an Optimization Problem

Find the orientation that rotates the known Earth gravity direction onto the measured acceleration direction. Express this as a minimization:

$$
\min_{\hat q\in\mathbb{R}^4} \tfrac12\|f(\hat q, {}^E\hat d, {}^S\hat s)\|^2,
\qquad
f = \hat q^* \otimes {}^E\hat d \otimes \hat q - {}^S\hat s
$$

Set ${}^E\hat d = \hat g = [0\,0\,0\,1]$ and ${}^S\hat s = \hat a$. The rotation $\hat q^*\otimes\hat g\otimes\hat q$ gives the third column of the rotation matrix. The objective function is therefore:

$$
f_g(\hat q, \hat a) =
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

Its Jacobian $J_g = \partial f_g/\partial \hat q$ is:

$$
J_g =
\begin{bmatrix}
-2q_3 & \ \ 2q_4 & -2q_1 & 2q_2\\
\ \ 2q_2 & \ \ 2q_1 & \ \ 2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### Gradient descent step

The gradient of the cost $\tfrac12\|f\|^2$ is:

$$
\nabla f = J_g^T f_g
$$

The update moves the estimate against this gradient:

$$
q_{\nabla,t} = \hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick takes only one step per sample. That is sufficient as long as the convergence rate matches the physical rotation rate: $\mu_t = \alpha\|\dot q_{\omega,t}\|\Delta t$ with $\alpha>1$.

Expanding $J_g^Tf_g$ explicitly (useful for embedded code):

$$
\nabla f =
\begin{bmatrix}
4q_1q_3^2 + 2q_3a_x + 4q_1q_2^2 - 2q_2a_y\\
4q_2q_4^2 - 2q_4a_x + 4q_1^2q_2 - 2q_1a_y - 4q_2 + 8q_2^3 + 8q_2q_3^2 + 4q_2a_z\\
4q_1^2q_3 + 2q_1a_x + 4q_3q_4^2 - 2q_4a_y - 4q_3 + 8q_3q_2^2 + 8q_3^3 + 4q_3a_z\\
4q_2^2q_4 - 2q_2a_x + 4q_3^2q_4 - 2q_3a_y
\end{bmatrix}
$$

These expressions use the unit-norm constraint $q_1^2+q_2^2+q_3^2+q_4^2=1$ to simplify.

---

## 4. Fusion

Blend the gradient-descent estimate and the gyro estimate with a weight $\gamma_t$:

$$
\hat q_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}
$$

Choose $\gamma_t$ so that the gyro divergence rate $\beta$ balances the accelerometer convergence rate:

$$
(1-\gamma_t)\beta = \gamma_t\frac{\mu_t}{\Delta t}
\;\Rightarrow\;
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

When $\mu_t$ is large (step size $\alpha \to$ large), $\gamma_t\approx \beta\Delta t/\mu_t \ll 1$. Substituting this, the $\hat q_{est,t-1}$ terms cancel, and the result is the well-known Madgwick update:

$$
\boxed{\;
\dot{\hat q}_{est,t} = \tfrac12\,\hat q_{est,t-1}\otimes{}^S\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}
\;}
$$

$$
\boxed{\;
\hat q_{est,t} = \frac{\hat q_{est,t-1} + \dot{\hat q}_{est,t}\Delta t}{\left\|\hat q_{est,t-1} + \dot{\hat q}_{est,t}\Delta t\right\|}
\;}
$$

Interpretation: the gyro term propagates the orientation, and the normalized gradient term removes the error component along the direction the accelerometer can observe, which is roll and pitch. Yaw is unobservable without a magnetometer, so it remains gyro-only and will drift.

---

## 5. Choosing β

$\beta$ represents the gyroscope measurement error expressed as a quaternion rate. Assume the gyro error is $\tilde\omega_\beta$ (rad/s) on each axis. Then:

$$
\beta = \left\|\tfrac12\hat q\otimes[0\ \tilde\omega_\beta\ \tilde\omega_\beta\ \tilde\omega_\beta]\right\| = \sqrt{\tfrac34}\,\tilde\omega_\beta
$$

For example, an error of 3°/s ≈ 0.052 rad/s gives β ≈ 0.045. The trade-off:

- **Larger β:** faster convergence, but more accelerometer noise and more sensitivity to linear acceleration.
- **Smaller β:** smoother output, but more gyro drift.

### Optional gyro bias compensation

Estimate the gyro bias from the error direction:

$$
\omega_\epsilon = 2\,\hat q^*_{est,t-1}\otimes\frac{\nabla f}{\|\nabla f\|},\qquad
\omega_b \mathrel{+}= \zeta\,\omega_\epsilon\Delta t,\qquad
\omega_c = \omega - \omega_b
$$

Here $\zeta=\sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the rate at which the gyro bias dr