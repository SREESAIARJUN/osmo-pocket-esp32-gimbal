# Madgwick Filter Derivation: Gyro and Accelerometer (IMU) Form

I don't have your project's "Cycle 13" context, so this derivation is general. Gimbal-payload considerations are covered in Section 7. Tell me your sensor axes, sample rate, or IMU mounting and I can tailor it.

---

## 1. Notation and Conventions

- The quaternion **q** = [q₀, q₁, q₂, q₃]ᵀ (scalar first) is ^S_E q̂. It describes the orientation of the Earth frame E relative to the sensor/payload frame S, which is Madgwick's convention.
- ⊗ is the Hamilton product and q* is the conjugate.
- The gyroscope gives ^Sω = [ωx, ωy, ωz] in rad/s.
- The accelerometer gives ^Sa = [ax, ay, az], normalized to unit length.
- The Earth gravity reference direction is ^E g = [0, 0, 0, 1] (z-up, "pure" quaternion form).

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation from the angular rate is:

$$
\dot{\mathbf q}_{\omega} = \tfrac12\, \mathbf q \otimes \begin{bmatrix}0 & \omega_x & \omega_y & \omega_z\end{bmatrix}^T
$$

Expanded:

$$
\dot{\mathbf q}_{\omega} = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone drifts, because gyro bias and noise accumulate.

---

## 3. Accelerometer Correction as an Optimization Problem

We seek the q that rotates the known Earth gravity direction into the measured sensor-frame direction:

$$
\min_{\mathbf q}\ \tfrac12\|\mathbf f(\mathbf q)\|^2,\qquad
\mathbf f(\mathbf q, {}^E\mathbf d, {}^S\mathbf s) = \mathbf q^* \otimes {}^E\mathbf d \otimes \mathbf q - {}^S\mathbf s
$$

With d = [0,0,0,1] and s = [0, ax, ay, az], the vector part reduces to:

$$
\mathbf f_g(\mathbf q, {}^S\mathbf a)=
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

These are the third row of the rotation matrix (gravity seen in the sensor frame) minus the measurement.

### Jacobian

$$
\mathbf J_g(\mathbf q)=
\begin{bmatrix}
-2q_2 & 2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & 2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### Gradient

$$
\nabla f = \mathbf J_g^T\,\mathbf f_g
$$

Writing f = [f₁, f₂, f₃]:

$$
\nabla f=
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2\\
\;\;2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\;\;2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

A single gradient-descent step per sample is sufficient, provided the step size keeps pace with the true rate of orientation change. Madgwick therefore uses the **normalized** gradient direction:

$$
\hat{\nabla} f = \frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion: The Madgwick Update Law

Combine gyro propagation with a correction along the negative gradient:

$$
\boxed{\;\dot{\mathbf q}_{est,t} = \dot{\mathbf q}_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

Discrete integration at sample period Δt:

$$
\boxed{\;\mathbf q_{t} = \mathbf q_{t-1} + \dot{\mathbf q}_{est,t}\,\Delta t,\qquad
\mathbf q_t \leftarrow \frac{\mathbf q_t}{\|\mathbf q_t\|}\;}
$$

This is effectively a complementary filter:
- The gyro dominates at high frequency.
- The accelerometer gradient pulls the estimate toward the gravity-consistent attitude at low frequency.

---

## 5. Choosing β (Filter Gain)

β is the magnitude of the gyro measurement error expressed as a quaternion rate. For a per-axis gyro error ω̃_β (rad/s):

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

The √(3/4) comes from mapping equal error on three gyro axes through the quaternion derivative.

The trade-off:
- **Larger β** converges faster but passes more accelerometer noise and linear-acceleration error into attitude.
- **Smaller β** is smoother but slower to correct drift.

Typical values are 0.03–0.1. A common practice is to start high (≈1–2.5) for initial convergence, then drop to the nominal value.

---

## 6. Optional Gyro Bias Compensation (ζ term)

Convert the normalized gradient back into an angular-rate error:

$$
\boldsymbol\omega_{\epsilon,t} = 2\,\hat{\mathbf q}_{t-1}^* \otimes \hat\nabla f
$$

Integrate it to track the bias, then subtract the bias before computing the gyro rate quaternion:

$$
\boldsymbol\omega_{b,t} = \boldsymbol\omega_{b,t-1} + \zeta\,\boldsymbol\omega_{\epsilon,t}\,\Delta t,
\qquad \boldsymbol\omega_c = \boldsymbol\omega - \boldsymbol\omega_b
$$

Here ζ = √(3/4)·ω̇̃_ζ, where ω̇̃_ζ is the estimated bias drift rate. Note that with accelerometer-only correction, only roll and pitch bias are observable.

---

## 7. Gimbal-Payload Considerations

1. **Yaw is unobservable.** Gravity constrains only roll and pitch, so yaw will drift. Use a magnetometer (MARG version), motor encoders, or the base IMU combined with joint angles as a heading reference. Magnetometers near gimbal motors are usually unreliable.

2. **Non-gravitational acceleration.** If the IMU sits at offset **r** from the rotation center, it measures:

   $$
   {}^S\mathbf a_{meas} = {}^S\mathbf g + \boldsymbol\omega\times(\boldsymbol\omega\times\mathbf r) + \dot{\boldsymbol\omega}\times\mathbf r + \mathbf a_{base}
   $$

   Either subtract the centripetal and tangential terms using the known **r**, or gate the correction adaptively. For example, set β → 0 (or scale it down) when the measured acceleration magnitude departs from 1 g by more than a threshold such as 5–10%. The gate can also reference |ω|.

3. **Vibration.** Low-pass the accelerometer, or rely on a small β. Motor PWM harmonics alias easily, so sample the IMU fast (≥1 kHz) and run the filter at ≥200–500 Hz.

4. **Frame alignment.** Rotate raw IMU axes into the payload frame before fusing, so the output quaternion directly gives payload pitch, roll, and yaw for the stabilization loop.

5. **Rate loop.** Feed the stabilization rate loop with bias-corrected gyro data directly, not the derivative of the filtered attitude. Use the Madgwick attitude for the outer angle loop.

---

## 8. Per-Sample Algorithm Summary

1. Read ω (rad/s) and **a**. Subtract the gyro bias if estimated.
2. If ‖**a**‖ ≈ 1 g is not satisfied (after norm