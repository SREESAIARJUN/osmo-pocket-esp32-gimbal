# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Form)

## 1. Conventions

- Orientation quaternion: $\hat{q} = {}^S_E\hat{q} = [q_0,\ q_1,\ q_2,\ q_3]^T$ (scalar first). It describes the Earth frame $E$ relative to the sensor/payload frame $S$.
- Gyroscope measurement in rad/s: ${}^S\omega = [0,\ \omega_x,\ \omega_y,\ \omega_z]$.
- Accelerometer measurement, normalized: ${}^S\hat{a} = [0,\ a_x,\ a_y,\ a_z]$, with $\|a\| = 1$.
- Earth gravity reference direction: ${}^E\hat{g} = [0,\ 0,\ 0,\ 1]$.
- Quaternion product: $\otimes$. Conjugate: $\hat{q}^*$.

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation from the measured angular rate is

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\,{}^S_E\hat{q}_{est,t-1}\otimes {}^S\omega_t
$$

Expanding the product gives

$$
\dot{q}_\omega = \tfrac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone drifts, because gyro bias and noise accumulate. The accelerometer is used to correct roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

We want the orientation that rotates the Earth reference direction onto the measured sensor direction:

$$
\min_{\hat{q}\in\mathbb{R}^4} \tfrac{1}{2}\|f(\hat{q}, {}^E\hat{d}, {}^S\hat{s})\|^2,
\qquad
f = \hat{q}^*\otimes{}^E\hat{d}\otimes\hat{q} - {}^S\hat{s}
$$

### 3.1 Objective function

Substitute $\hat{d} = [0,0,0,1]$ and $\hat{s} = \hat{a}$. The term $\hat{q}^*\otimes\hat{d}\otimes\hat{q}$ is the third column of the rotation matrix, so

$$
f_g(\hat{q},\hat{a}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### 3.2 Jacobian

Take the partial derivative of each row of $f_g$ with respect to $q_0, q_1, q_2, q_3$:

$$
J_g(\hat{q}) =
\begin{bmatrix}
-2q_2 & \ \ 2q_3 & -2q_0 & \ \ 2q_1\\
\ \ 2q_1 & \ \ 2q_0 & \ \ 2q_3 & \ \ 2q_2\\
\ \ 0 & -4q_1 & -4q_2 & \ \ 0
\end{bmatrix}
$$

### 3.3 Gradient

$$
\nabla f = J_g^T f_g =
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2\\
\ \ 2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\ \ 2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

A single gradient-descent step is

$$
\hat{q}_{\nabla,t} = \hat{q}_{est,t-1} - \mu_t\frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion

### 4.1 Rate-form approximation

Madgwick sets the step size proportional to the convergence rate:

$$
\mu_t = \alpha\,\|\dot{q}_\omega\|\Delta t, \qquad \alpha > 1
$$

Consequently, one gradient step per sample is sufficient.

### 4.2 Complementary fusion

The two estimates are combined with a weight $\gamma$:

$$
\hat{q}_{est} = \gamma\,\hat{q}_\nabla + (1-\gamma)\,\hat{q}_\omega
$$

The weight $\gamma$ is chosen so that the divergence rate of $\hat{q}_\omega$, which is bounded by $\beta$, equals the convergence rate of $\hat{q}_\nabla$.

With $\alpha$ large, $\gamma \approx \beta\Delta t/\mu_t$ becomes small. Converting the result back to rate form gives the core equation:

$$
\boxed{\ \dot{q}_{est,t} = \dot{q}_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\ }
$$

### 4.3 Integration and normalization

$$
\boxed{\ \hat{q}_{est,t} = \frac{\hat{q}_{est,t-1} + \dot{q}_{est,t}\,\Delta t}{\left\|\hat{q}_{est,t-1} + \dot{q}_{est,t}\,\Delta t\right\|}\ }
$$

---

## 5. Choosing β

Suppose each gyro axis has a zero-mean error of magnitude $\tilde{\omega}_\beta$. The resulting quaternion-rate error is

$$
\left\|\tfrac12\,\hat{q}\otimes[0,\tilde{\omega}_\beta,\tilde{\omega}_\beta,\tilde{\omega}_\beta]\right\|
$$

which gives

$$
\beta = \sqrt{\tfrac{3}{4}}\;\tilde{\omega}_\beta
$$

For example, a gyro error of 3°/s gives $\beta \approx 0.045$.

Trade-off:
- Larger β converges faster but passes through more accelerometer disturbance.
- Smaller β is smoother but corrects drift more slowly.

---

## 6. Optional Gyro-Bias Compensation

The normalized gradient can be mapped back to an angular-rate error:

$$
\omega_{\varepsilon} = 2\,\hat{q}^*\otimes\frac{\nabla f}{\|\nabla f\|}
$$

Integrate it into a bias estimate and subtract that from the gyro reading:

$$
\omega_b = \zeta\!\int\!\omega_\varepsilon\,dt, \qquad \omega_c = \omega - \omega_b
$$

Choose $\zeta = \sqrt{3/4}\,\dot{\tilde{\omega}}_\zeta$, where $\dot{\tilde{\omega}}_\zeta$ is the gyro bias drift rate.

---

## 7. Gimbal-Payload Considerations

1. **Non-gravitational acceleration.** If the IMU is offset by $r$ from the gimbal's rotation center, it measures extra terms:
   $$a_{meas} = g + a_{base} + \dot{\omega}\times r + \omega\times(\omega\times r)$$
   - Mount the IMU at the rotation center where possible, or subtract these terms.
   - Gate the correction: set β to 0 or reduce it when $\big|\,\|a_{raw}\| - 9.81\big| > \epsilon$, for example 0.1 g.
2. **Yaw is unobservable** from gravity alone; the fourth gradient component carries no heading information. Bound yaw drift using motor encoders or a magnetometer, which requires the MARG form of the filter.
3. **Rate.** Run the filter at or above the gimbal control loop rate (≥ 500 Hz typical), using the measured Δt per sample.
4. **Initialization.** Set the initial roll and pitch from the first accelerometer samples:
   - $\phi_0 = \operatorname{atan2}(a_y, a_z)$
   - $\theta_0 = \operatorname{atan2}(-a_x, \sqrt{a_y^2 + a_z^2})$
   
   Optionally use a high β for the first ~