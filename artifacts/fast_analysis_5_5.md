# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Form)

I don't have the specifics of your "Cycle 5" (sample rate, sensor specs, or gimbal geometry), so this derivation is general. Where your payload's numbers matter, I've flagged them and shown how to plug them in.

---

## 1. Conventions

- Quaternion: **q** = [q₁ q₂ q₃ q₄]ᵀ = [w x y z]ᵀ, unit norm. It describes the orientation of the **Earth frame E relative to the sensor frame S**: ˢₑq.
- Quaternion product: ⊗. Conjugate: q* = [q₁ −q₂ −q₃ −q₄]ᵀ.
- Gyro measurement (rad/s), written as a pure quaternion: ˢω = [0 ωₓ ω_y ω_z]ᵀ.
- Accelerometer measurement, normalized: ˢâ = [0 aₓ a_y a_z]ᵀ, with ‖â‖ = 1.
- Gravity reference in the Earth frame (z-up, normalized): ᴱĝ = [0 0 0 1]ᵀ.

---

## 2. Gyroscope Path: Quaternion Kinematics

The orientation rate due to body angular velocity is

$$
\dot{\mathbf q}_{\omega,t} = \tfrac12\, \hat{\mathbf q}_{est,t-1} \otimes {}^S\boldsymbol\omega_t
$$

Expanded:

$$
\dot{\mathbf q}_{\omega} = \tfrac12
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z\\
\;\;q_1\omega_x + q_3\omega_z - q_4\omega_y\\
\;\;q_1\omega_y - q_2\omega_z + q_4\omega_x\\
\;\;q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

Integrating this alone gives the gyro-only estimate, which drifts because bias and noise accumulate.

---

## 3. Accelerometer Path: Orientation as an Optimization Problem

Find the q that rotates the known gravity direction into the measured direction. Minimize:

$$
\min_{\mathbf q}\; \tfrac12\|\mathbf f(\mathbf q,{}^E\hat g,{}^S\hat a)\|^2,\qquad
\mathbf f = \mathbf q^*\otimes{}^E\hat g\otimes\mathbf q - {}^S\hat a
$$

With ᴱĝ = [0 0 0 1], the rotated gravity vector (the third column of the rotation matrix) gives:

$$
\mathbf f_g(\mathbf q,\hat a)=
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

### Jacobian

Differentiate each row with respect to (q₁, q₂, q₃, q₄):

$$
\mathbf J_g(\mathbf q)=
\begin{bmatrix}
-2q_3 & 2q_4 & -2q_1 & 2q_2\\
\;\;2q_2 & 2q_1 & \;\;2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### Gradient

$$
\nabla f = \mathbf J_g^T\,\mathbf f_g
$$

Scalar form, with f₁, f₂, f₃ the rows of f_g:

$$
\begin{aligned}
s_1 &= -2q_3 f_1 + 2q_2 f_2\\
s_2 &= \;\;2q_4 f_1 + 2q_1 f_2 - 4q_2 f_3\\
s_3 &= -2q_1 f_1 + 2q_4 f_2 - 4q_3 f_3\\
s_4 &= \;\;2q_2 f_1 + 2q_3 f_2
\end{aligned}
$$

A single gradient-descent step per sample is sufficient, provided the step size is at least as large as the rate of orientation change. This is Madgwick's key insight. The step direction is the normalized gradient:

$$
\dot{\mathbf q}_{\epsilon} = \frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion: Combining Both Paths

The gyro rate is corrected by subtracting a fixed-magnitude step along the accelerometer error gradient:

$$
\boxed{\;\dot{\mathbf q}_{est,t} = \tfrac12\,\hat{\mathbf q}_{est,t-1}\otimes{}^S\boldsymbol\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

Integrate and renormalize:

$$
\boxed{\;\mathbf q_{est,t} = \hat{\mathbf q}_{est,t-1} + \dot{\mathbf q}_{est,t}\,\Delta t,\qquad
\hat{\mathbf q}_{est,t} = \frac{\mathbf q_{est,t}}{\|\mathbf q_{est,t}\|}\;}
$$

### Choosing β

β represents the gyro measurement error expressed as a quaternion rate. If each gyro axis has zero-mean error of magnitude ω̃_β (rad/s):

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

- **Larger β:** faster convergence, but more accelerometer noise and vibration in the estimate.
- **Smaller β:** smoother, but slower correction of drift.
- **Gimbal example:** for a MEMS gyro, ω̃_β ≈ 2–5°/s gives β ≈ 0.03–0.08. Insert your measured gyro noise here.

---

## 5. Optional: Gyro Bias Compensation

The filter can also estimate gyro bias from the gradient direction:

$$
\boldsymbol\omega_{\epsilon,t} = 2\,\hat{\mathbf q}^*_{est,t-1}\otimes\frac{\nabla f}{\|\nabla f\|},\qquad
\boldsymbol\omega_{b,t} = \zeta\sum_t \boldsymbol\omega_{\epsilon,t}\Delta t,\qquad
\boldsymbol\omega_{c,t} = \boldsymbol\omega_t - \boldsymbol\omega_{b,t}
$$

Then use ω_c in place of ω in Section 2. Set ζ = √(3/4) · ω̇̃_ζ, where ω̇̃_ζ is the expected bias drift rate. ζ is typically about 10–100× smaller than β.

---

## 6. Discrete Algorithm (per sample)

```
1. Read ω (rad/s) and a; if ‖a‖ ≈ 0, skip the correction step
2. â = a/‖a‖
3. qdot = 0.5 * q ⊗ [0, ω]
4. Compute f_g (Section 3) and s = J_gᵀ f_g; s = s/‖s‖
5. qdot = qdot − β*s
6. q = q + qdot*Δt;  q = q/‖q‖
```

---

## 7. Gimbal-Payload Considerations

1. **Yaw is unobservable.** Gravity constrains only roll and pitch, so yaw drifts at the residual gyro bias rate. For an absolute heading you need a magnetometer (MARG form, which adds a field reference b = [0 b_x 0 b_z]), a motor encoder, or a heading from the vehicle's IMU.

2. **Accelerations other than gravity corrupt the reference.** The accelerometer also senses:
   - Vehicle linear acceleration.
   - Gimbal-induced accelerations if the IMU is offset from the rotation axes: centripetal ω×(ω×r) and tangential ω̇×r.

   **Mitigations:**
   - Mount the IMU near the axis intersection.
   - Subtract the lever-arm terms if r is known.
   - Gate or attenuate the correction, e.g. set β → 0 or reduce it when |‖a‖ − g| > ~0.05 g or ‖ω‖ is large.

3. **Sample rate.** At gimbal loop rates of 500 Hz–2 kHz, first-order integration is adequate. Use the actual measured Δt, not the nominal one.

4. **Initialization.** Initialize roll and pitch from the accelerometer while the gimbal is stationary, or start with a large β for the first ~1 s and then reduce it to the nominal value.

5. **Axis alignment.** Rotate the raw IMU axes into the payload frame before filtering. Gyro and accelerometer must share the same frame and handedness.

If you share your Cycle 5 parameters (gyro noise density, sample rate, and IMU offset from the gim