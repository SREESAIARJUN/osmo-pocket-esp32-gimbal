# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Form)

I don't have the specifics of your "Cycle 4" gimbal payload, such as IMU part, sample rate or mounting. This derivation is general and fully sensor-agnostic. Gimbal-specific tuning notes are at the end.

---

## 1. Conventions

- Quaternion (scalar first, unit norm): **q** = [q₀, q₁, q₂, q₃]ᵀ. It represents the orientation of the **earth frame E relative to the sensor frame S**, written ˢₑ**q**.
- Quaternion product: ⊗. Conjugate: **q**\* = [q₀, −q₁, −q₂, −q₃].
- Gyroscope measurement (rad/s), as a pure quaternion: ˢ**ω** = [0, ωₓ, ω_y, ω_z].
- Accelerometer measurement, normalized: ˢ**â** = [0, aₓ, a_y, a_z], with ‖**a**‖ = 1.

---

## 2. Gyroscope Kinematics (Prediction)

The rate of change of orientation from angular velocity is:

$$
\dot{\mathbf q}_{\omega,t} = \tfrac{1}{2}\,\hat{\mathbf q}_{est,t-1} \otimes {}^S\boldsymbol\omega_t
$$

Expanded:

$$
\dot{\mathbf q}_\omega = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone drifts, because gyro bias and noise accumulate. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

We seek the orientation **q** that rotates a known earth-frame reference into the measured sensor-frame vector.

- **Reference:** gravity direction in E, ᴱ**d** = [0, 0, 0, 1].
- **Measurement:** ˢ**s** = ˢ**â**.

The objective function is:

$$
\mathbf f(\mathbf q, {}^E\mathbf d, {}^S\mathbf s) = \mathbf q^* \otimes {}^E\mathbf d \otimes \mathbf q - {}^S\mathbf s
\qquad\Rightarrow\qquad
\min_{\mathbf q}\ \tfrac12\|\mathbf f\|^2
$$

### 3.1 Evaluate the objective for the gravity reference

Rotating [0, 0, 1] into the sensor frame gives the third column of the rotation matrix. Therefore:

$$
\mathbf f_g(\mathbf q, {}^S\hat{\mathbf a}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### 3.2 Jacobian

Differentiate each row of **f**_g with respect to [q₀, q₁, q₂, q₃]:

$$
\mathbf J_g(\mathbf q)=
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 3.3 Gradient

$$
\nabla \mathbf f = \mathbf J_g^{T}(\hat{\mathbf q}_{est,t-1})\;\mathbf f_g(\hat{\mathbf q}_{est,t-1}, {}^S\hat{\mathbf a}_t)
$$

Multiplying out and simplifying with ‖**q**‖ = 1 gives the form used in practice. This is the actual gradient scaled by 2, which is irrelevant because the gradient is normalized in the next step:

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

### 3.4 One gradient-descent step

Madgwick showed that a single step per sample is sufficient, provided the convergence rate matches or exceeds the physical rate of orientation change:

$$
\mathbf q_{\nabla,t} = \hat{\mathbf q}_{est,t-1} - \mu_t\,\frac{\nabla\mathbf f}{\|\nabla\mathbf f\|}
$$

---

## 4. Fusing the Two Estimates

Madgwick originally fuses the two estimates with a complementary weight γ:

$$
\mathbf q_{est,t} = \gamma_t\,\mathbf q_{\nabla,t} + (1-\gamma_t)\,\mathbf q_{\omega,t}
$$

The optimal γ makes the gyro-driven divergence rate β equal to the gradient convergence rate μ/Δt. Assuming μ is large, so that γ is small, the fusion collapses to a single rate equation:

$$
\boxed{\;\dot{\hat{\mathbf q}}_{est,t} = \tfrac12\,\hat{\mathbf q}_{est,t-1}\otimes{}^S\boldsymbol\omega_t \;-\; \beta\,\frac{\nabla\mathbf f}{\|\nabla\mathbf f\|}\;}
$$

### 4.1 Integration and normalization

$$
\hat{\mathbf q}_{est,t} = \hat{\mathbf q}_{est,t-1} + \dot{\hat{\mathbf q}}_{est,t}\,\Delta t,
\qquad
\hat{\mathbf q}_{est,t} \leftarrow \frac{\hat{\mathbf q}_{est,t}}{\|\hat{\mathbf q}_{est,t}\|}
$$

---

## 5. The Gain β

β represents the expected gyroscope error expressed as a quaternion rate. With ω̃_β the estimated mean zero-mean gyro error per axis (rad/s):

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

The √(3/4) arises because the error is spread equally across 3 axes and mapped through the ½ q ⊗ ω kinematics.

**Optional gyro-bias compensation (MARG paper, eq. 47–49):** estimate the bias from the gradient direction:

$$
{}^S\boldsymbol\omega_{\epsilon} = 2\,\hat{\mathbf q}^*_{est,t-1}\otimes \frac{\nabla\mathbf f}{\|\nabla\mathbf f\|},
\qquad
{}^S\boldsymbol\omega_{b,t} = \zeta\sum_t {}^S\boldsymbol\omega_{\epsilon,t}\Delta t
$$

Then use **ω**_c = **ω** − **ω**_b in Section 2. The gain ζ is set as ζ = √(3/4) ω̇̃_ζ, where ω̇̃_ζ is the bias drift rate.

---

## 6. Implementation (per sample)

```c
// inputs: g