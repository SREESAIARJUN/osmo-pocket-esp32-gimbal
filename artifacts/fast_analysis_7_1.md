# Madgwick Filter: Quaternion Orientation Update (Gyro + Accelerometer)

*I don't have your project's "Cycle 7" documents, so this uses standard Madgwick notation. Adapt the axis conventions and IMU mounting to your payload.*

---

## 1. Conventions

- Orientation quaternion of the sensor frame **S** relative to the earth frame **E**:
  $$\hat{q} = {}^S_E\hat{q} = [q_1\; q_2\; q_3\; q_4]^T,\qquad q_1 = \text{scalar part},\quad \|\hat q\|=1$$
- Quaternion product: ⊗. Conjugate: $\hat q^* = [q_1, -q_2, -q_3, -q_4]$.
- A vector $v$ is embedded as a pure quaternion $[0, v_x, v_y, v_z]$.
- Earth frame is z-up. Gravity reference: $\hat{d}_E = [0\;0\;0\;1]$.

---

## 2. Gyroscope Kinematics (Prediction)

The gyro measures angular rate in the sensor frame, $\omega_S = [0\;\omega_x\;\omega_y\;\omega_z]$ (rad/s). The quaternion derivative is:

$$\dot{q}_{\omega,t} = \tfrac{1}{2}\,\hat{q}_{est,t-1} \otimes \omega_{S,t}$$

Expanded:

$$\dot q_\omega = \tfrac12
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\;\;\,q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\;\;\,q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\;\;\,q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}$$

Integrating this alone drifts, because gyro bias and noise accumulate.

---

## 3. Accelerometer Correction as an Optimization Problem

We seek the orientation that rotates the known earth-frame gravity direction onto the measured, normalized accelerometer vector $\hat a_S = [0\;a_x\;a_y\;a_z]$:

$$\min_{\hat q}\; \tfrac12\|f(\hat q, \hat d_E, \hat a_S)\|^2,\qquad
f = \hat q^* \otimes \hat d_E \otimes \hat q - \hat a_S$$

With $\hat d_E = [0,0,0,1]$, the objective function reduces to:

$$f_g(\hat q,\hat a_S)=
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}$$

Its Jacobian is:

$$J_g(\hat q)=
\begin{bmatrix}
-2q_3 & \;\;2q_4 & -2q_1 & 2q_2\\
\;\;2q_2 & \;\;2q_1 & \;\;2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}$$

The gradient of the cost is:

$$\nabla f = J_g^T(\hat q_{est,t-1})\, f_g(\hat q_{est,t-1}, \hat a_{S,t})$$

**Single gradient-descent step.** Madgwick argues that one step per sample is sufficient, provided the convergence rate of the step is at least the rate at which the true orientation changes. The step direction is the normalized gradient:

$$\dot q_{\epsilon,t} = \frac{\nabla f}{\|\nabla f\|}$$

---

## 4. Fusion Equation

Subtract the gradient-correction term from the gyro rate. This acts like a complementary filter in quaternion-derivative space:

$$\boxed{\dot q_{est,t} = \dot q_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}}$$

Then integrate and normalize:

$$\hat q_{est,t} = \hat q_{est,t-1} + \dot q_{est,t}\,\Delta t,\qquad
\hat q_{est,t} \leftarrow \frac{\hat q_{est,t}}{\|\hat q_{est,t}\|}$$

**Choosing β.** β is the magnitude of the gyro measurement error, expressed as a quaternion derivative. If $\tilde\omega_\beta$ is the expected per-axis gyro error (rad/s):

$$\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta$$

- Larger β: faster convergence and stronger accelerometer trust, but noisier output.
- Smaller β: smoother output, but more drift.
- Typical values are 0.03–0.1. Use a high β at start-up for fast alignment, then decay it.

---

## 5. Optional Gyro Bias Compensation

Estimate the angular error from the correction direction:

$$\omega_{\epsilon,t} = 2\,\hat q^*_{est,t-1}\otimes \dot q_{\epsilon,t}$$

Integrate to track bias, then subtract it from the gyro reading before step 2:

$$\omega_{b,t} = \omega_{b,t-1} + \zeta\,\omega_{\epsilon,t}\Delta t,\qquad
\omega_{c,t} = \omega_{S,t} - \omega_{b,t}$$

Here $\zeta = \sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the expected bias drift rate. Keep ζ small.

---

## 6. Per-Sample Algorithm

```
1. Read gyro ω (rad/s) and accel a.
2. qDot = 0.5 * q ⊗ [0, ω]
3. If ||a|| is valid (see gating below):
       a = a / ||a||
       f    = f_g(q, a)
       grad = J_gᵀ f
       grad = grad / ||grad||
       qDot = qDot - β * grad
4. q = q + qDot * dt
5. q = q / ||q||
```

---

## 7. Gimbal Payload Considerations

1. **Non-gravitational acceleration.** The accelerometer actually measures
   $a_S = R^T(g + a_{lin}) + \alpha\times r + \omega\times(\omega\times r)$,
   where $r$ is the IMU offset from the gimbal's rotation center. You can:
   - mount the IMU near the rotation center, or subtract the centripetal and tangential terms using the measured ω and the known $r$;
   - gate the correction: skip it, or reduce β, when $\big|\|a\|-g\big| > \epsilon$ (e.g., 0.1 g) or when $\|\omega\|$ is large;
   - feed forward the host vehicle's acceleration if it is available.
2. **Yaw is unobservable** from gravity alone. Gravity constrains only roll and pitch, so yaw will drift. To fix yaw, add a magnetometer (Madgwick's MARG extension with $\hat b_E = [0, b_x, 0, b_z]$) or use the gimbal motor encoders and the vehicle heading.
3. **Rate and latency.** Run the filter at the gyro rate (≥ 500 Hz–1 kHz for stabilization loops) and use the measured Δt.
4. **Frame alignment.** Apply the IMU-to-payload mounting rotation before fusion. Also verify the gyro sign convention by checking that a positive rotation produces the expected change in the quaternion.