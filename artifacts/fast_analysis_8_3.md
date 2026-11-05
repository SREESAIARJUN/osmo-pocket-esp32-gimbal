# Madgwick Filter Derivation: Gyroscope and Accelerometer Fusion (IMU Form)

I don't have your Cycle 8 project documents, so this uses Madgwick's original conventions. Map the frames and axes to your payload IMU's mounting as needed.

---

## 1. Conventions

- The quaternion **q = [q₁ q₂ q₃ q₄]ᵀ** has the scalar first. It is ${}^S_E\hat q$, the orientation of the **Earth frame E relative to the sensor frame S** (the payload IMU).
- ⊗ is the Hamilton product, and q* is the conjugate.
- The gyroscope gives ${}^S\omega = [0\ \omega_x\ \omega_y\ \omega_z]$ in rad/s.
- The accelerometer gives ${}^S a = [0\ a_x\ a_y\ a_z]$, normalized to unit length.
- Gravity in the Earth frame is ${}^E\hat g = [0\ 0\ 0\ 1]$ (NED-up/ENU-up sign: choose one to match your accelerometer).

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation due to angular rate is:

$$
{}^S_E\dot q_{\omega,t} = \tfrac12\, {}^S_E\hat q_{t-1} \otimes {}^S\omega_t
$$

Expanded:

$$
\begin{aligned}
\dot q_1 &= \tfrac12(-q_2\omega_x - q_3\omega_y - q_4\omega_z)\\
\dot q_2 &= \tfrac12(\ \ q_1\omega_x + q_3\omega_z - q_4\omega_y)\\
\dot q_3 &= \tfrac12(\ \ q_1\omega_y - q_2\omega_z + q_4\omega_x)\\
\dot q_4 &= \tfrac12(\ \ q_1\omega_z + q_2\omega_y - q_3\omega_x)
\end{aligned}
$$

Integrated alone, this drifts because gyro noise and bias accumulate. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

Find the q that rotates the known Earth-frame field direction **d** onto the measured sensor-frame direction **s**:

$$
\min_{q}\ \tfrac12\|f(q,{}^E d,{}^S s)\|^2,\qquad
f = q^*\otimes {}^E d \otimes q - {}^S s
$$

### 3.1 Objective Function for Gravity

With $d = [0\,0\,0\,1]$ and $s = a$, f becomes:

$$
f_g(q,a)=
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x\\
2(q_1q_2 + q_3q_4) - a_y\\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

The first three terms are the third column of the rotation matrix, i.e., predicted gravity in the sensor frame.

### 3.2 Jacobian

$$
J_g(q)=\frac{\partial f_g}{\partial q}=
\begin{bmatrix}
-2q_3 & \ \ 2q_4 & -2q_1 & 2q_2\\
\ \ 2q_2 & \ \ 2q_1 & \ \ 2q_4 & 2q_3\\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### 3.3 Gradient

$$
\nabla f = J_g^T f_g
$$

Writing $f_g = [f_1, f_2, f_3]^T$:

$$
\begin{aligned}
\nabla f_1 &= -2q_3 f_1 + 2q_2 f_2\\
\nabla f_2 &= \ \ 2q_4 f_1 + 2q_1 f_2 - 4q_2 f_3\\
\nabla f_3 &= -2q_1 f_1 + 2q_4 f_2 - 4q_3 f_3\\
\nabla f_4 &= \ \ 2q_2 f_1 + 2q_3 f_2
\end{aligned}
$$

### 3.4 Gradient Descent Step

A gradient-descent step on the objective is:

$$
q_{k+1} = q_k - \mu \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick takes **one iteration per sample**. This is valid because the orientation changes little between samples.

---

## 4. Fusion

### 4.1 Combining the Two Estimates

The gyro-integrated estimate and the gradient-step estimate are blended with a weight γ:

$$
q_{est} = \gamma\, q_{\nabla} + (1-\gamma)\, q_{\omega}
$$

### 4.2 Optimal Weighting

Optimal weighting occurs when the gyro divergence rate equals the gradient convergence rate. Let μ be proportional to $\alpha\|\dot q_\omega\|\Delta t$ with α large. Then γ becomes small and the blend collapses to a single rate equation:

$$
\boxed{\;
{}^S_E\dot q_{est,t} = {}^S_E\dot q_{\omega,t} \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}
\;}
$$

$$
\boxed{\;
{}^S_E q_{est,t} = {}^S_E\hat q_{est,t-1} + {}^S_E\dot q_{est,t}\,\Delta t,
\qquad
{}^S_E\hat q_{est,t} = \frac{q_{est,t}}{\|q_{est,t}\|}
\;}
$$

### 4.3 Choosing β

β is the gyro measurement error expressed as a quaternion rate:

$$
\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta
$$

Here $\tilde\omega_\beta$ is the expected gyro error magnitude in rad/s per axis.

- Typical values are 0.03 to 0.1 for MEMS gyroscopes.
- A larger β converges faster but passes more accelerometer noise.

---

## 5. Optional Gyro Bias Compensation (ζ term)

The normalized gradient direction can be mapped back to an angular-rate error:

$$
\omega_\varepsilon = 2\,\hat q^*_{est,t-1}\otimes \frac{\nabla f}{\|\nabla f\|}
$$

Integrate this error to estimate the bias:

$$
\omega_b = \zeta \sum_t \omega_\varepsilon \Delta t
$$

Then correct the gyro reading before using it in Section 2:

$$
\omega_c = \omega - \omega_b
$$

Here $\zeta = \sqrt{\tfrac34}\,\tilde{\dot\omega}_\zeta$, the expected bias drift rate. It is typically very small, around 0.001 to 0.015.

---

## 6. Per-Sample Algorithm

```
1. Read ω (rad/s) and a. If ‖a‖ ≈ 0, skip correction.
2. a ← a/‖a‖
3. q̇_ω = ½ q ⊗ [0, ω]
4. Compute f_g (§3.1) and ∇f = J_gᵀ f_g (§3.3); normalize ∇f.
5. q̇ = q̇_ω − β ∇f̂
6. q ← q + q̇ Δt;  q ← q/‖q‖
7. Optional: convert to Euler angles for the gimbal loop
   roll  = atan2(2(q1q2+q3q4), 1−2(q2²+q3²))
   pitch = asin(2(q1q3−q4q2))
   yaw   = atan2(2(q1q4+q2q3), 1−2(q3²+q4²))
```

The Euler formulas are written for the conjugate (sensor-to-Earth) form. Conjugate q first if your loop expects that convention.

---

## 7. Gimbal-Payload Considerations

- **Yaw is unobservable** with gyro plus accelerometer alone, because gravity carries no heading information. Yaw will drift at the residual gyro bias rate. To fix this, do one of the following:
  - Add the magnetometer terms (f_b, J_b) to form the MARG version.
  - Use the base IMU or vehicle heading combined with the gimbal motor encoders.
- **Carrier acceleration corrupts gravity.** Platform maneuvers and vibration add linear acceleration to the accel