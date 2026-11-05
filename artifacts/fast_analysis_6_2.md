# Madgwick Filter Derivation: Gyroscope + Accelerometer Fusion for Gimbal Payload Orientation

I don't have context on what "Cycle 6" refers to in your project, so this is a self-contained derivation. I've flagged where gimbal-specific choices (frames, β tuning, vibration) would plug into your setup.

---

## 1. Conventions

- **Quaternion** ${}^S_E\hat{q} = [q_1\ q_2\ q_3\ q_4]^T = [q_w\ q_x\ q_y\ q_z]^T$: the orientation of the Earth frame E relative to the sensor (payload) frame S, with unit norm.
- **Product** $\otimes$ is the Hamilton product.
- **Earth frame**: NED or ENU. Here gravity is taken along +z of E, so ${}^E\hat{g} = [0\ 0\ 0\ 1]$ as a normalized pure quaternion.
- **Sensor frame S**: the IMU axes on the gimbal payload (camera) body, after any fixed mounting rotation is removed.

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The gyroscope measures angular rate in the sensor frame, written as a pure quaternion:

$${}^S\omega = [0\ \ \omega_x\ \ \omega_y\ \ \omega_z]$$

The quaternion rate of change is:

$${}^S_E\dot{q}_{\omega} = \tfrac{1}{2}\,{}^S_E\hat{q} \otimes {}^S\omega$$

Expanded:

$$
\dot q_\omega = \tfrac12
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\ \ q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\ \ q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\ \ q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

Integrating the gyroscope alone gives ${}^S_E q_{\omega,t} = \hat q_{t-1} + \dot q_{\omega,t}\,\Delta t$. This estimate drifts because of gyro bias and noise.

---

## 3. Accelerometer Correction as an Optimization Problem

**Assumption:** the payload is quasi-static, so the accelerometer measures mostly gravity. The normalized accelerometer reading is ${}^S\hat a = [0\ a_x\ a_y\ a_z]$.

We seek the orientation that rotates the known Earth gravity direction into the measured direction. This is expressed as minimizing an objective function:

$$\min_{q}\ \tfrac12\|f(q, {}^E\hat g, {}^S\hat a)\|^2,\qquad f = q^* \otimes {}^E\hat g \otimes q - {}^S\hat a$$

With ${}^E\hat g = [0\ 0\ 0\ 1]$, this expands to:

$$
f_g(q,{}^S\hat a)=
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

### Jacobian

$$
J_g(q)=
\begin{bmatrix}
-2q_3 & \ \ 2q_4 & -2q_1 & 2q_2\\
\ \ 2q_2 & \ \ 2q_1 & \ \ 2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### Gradient

$$\nabla f = J_g^T(\hat q_{t-1})\, f_g(\hat q_{t-1}, {}^S\hat a_t)$$

Written out explicitly:

$$
\nabla f=
\begin{bmatrix}
-2q_3 f_1 + 2q_2 f_2\\
\ \ 2q_4 f_1 + 2q_1 f_2 - 4q_2 f_3\\
-2q_1 f_1 + 2q_4 f_2 - 4q_3 f_3\\
\ \ 2q_2 f_1 + 2q_3 f_2
\end{bmatrix}
$$

### Gradient Descent Step

Madgwick's key insight is that a single gradient-descent step per sample is sufficient, provided the step size keeps pace with the rate of orientation change. The correction direction is therefore the normalized gradient:

$$\dot q_{\epsilon} = \frac{\nabla f}{\|\nabla f\|}$$

---

## 4. Fusion: Combining the Two Estimates

Rather than blending two separate quaternions with a weighting, the accelerometer correction is subtracted directly from the gyroscope rate:

$$\boxed{\ {}^S_E\dot q_{est,t} = {}^S_E\dot q_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\ }$$

$$\boxed{\ {}^S_E q_{est,t} = {}^S_E\hat q_{est,t-1} + {}^S_E\dot q_{est,t}\,\Delta t,\qquad \hat q_{est,t} = \frac{q_{est,t}}{\|q_{est,t}\|}\ }$$

### Choosing β

Madgwick ties β to the gyroscope measurement error. If each gyro axis has error $\tilde\omega_\beta$ (in rad/s), then:

$$\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta$$

The two extremes behave as follows:

| β | Behavior |
|---|----------|
| Small | Trusts the gyro. Output is smooth and rejects vibration, but drift correction is slow. |
| Large | Trusts the accelerometer. Converges fast, but is sensitive to linear acceleration and vibration. |

### Optional Gyro-Bias Compensation (MARG Extension)

The filter can also estimate and remove gyro bias:

$$\omega_{\epsilon} = 2\,\hat q^* \otimes \dot q_\epsilon,\qquad \omega_b \mathrel{+}= \zeta\,\omega_\epsilon\Delta t,\qquad \omega_c = \omega - \omega_b$$

---

## 5. Implementation Summary (per sample)

```
q = [q1 q2 q3 q4]; gx,gy,gz in rad/s; ax,ay,az raw
qDot = 0.5 * quatMult(q, [0 gx gy gz])
if |a| > 0:
    a  = a/|a|
    f  = [2(q2q4 - q1q3) - ax,
          2(q1q2 + q3q4) - ay,
          1 - 2(q2² + q3²) - az]
    J  = [[-2q3, 2q4,-2q1, 2q2],
          [ 2q2, 2q1, 2q4, 2q3],
          [  0, -4q2,-4q3,  0 ]]
    s  = Jᵀ f ;  s = s/|s|
    qDot = qDot - beta*s
q = q + qDot*dt ;  q = q/|q|
```

---

## 6. Gimbal Payload Considerations

1. **Yaw is unobservable.** With gyro and accelerometer only, the gradient has no component about gravity, so heading drifts. To correct it, add a magnetometer (often unreliable near gimbal motors) or use the yaw encoder and base IMU kinematics.
2. **Linear acceleration.** During vehicle maneuvers, consider gating or reducing β when $\big|\,\|a\| - g\,\big| > \text{threshold}$.
3. **Motor vibration.** Low-pass filter the accelerometer before normalizing. Also keep β small, since the stabilization loop relies on a smooth attitude estimate.
4. **Mounting.** Rotate raw IMU data into the payload frame with a fixed calibration quaternion before running the filter.
5. **Rate.** Run the filter at or above the control-loop rate (typically 500 Hz–1 kHz), and use the measured Δt rather than a nominal value.
6. **Initialization.** Seed $q_0$ from accelerometer tilt (roll and pitch from $a$, yaw = 0), or use a large β for the first ~1 s to converge quickly.

---

## 7. Output Euler Angles (for Gimbal Control)

$$
\phi=\operatorname{atan2}\!\big(2(q_1q_2+q_3q_4),\,1-2(q_2^2+q_3^2)\big)
$$

$$
\theta=\arcsin\!\big(2(q_1q_3-q_4q_2)\big)
$$

$$
\psi=\operatorname{atan2}\!\big(2(q_1q_4+q_2q_3),\,1-2(q_3^2+q_4^2)\big)
$$

Use these for roll/pitch error feedback. Keep the quaternion internally to avoid the gimbal-lock singularity at θ = ±90°.

If you share what Cycle 6 specifies (sensor rates, frame convention, or whether a magnetometer is present), I can tailor β, the frame definitions, and the yaw handling.