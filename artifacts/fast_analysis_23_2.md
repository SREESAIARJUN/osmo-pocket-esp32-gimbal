# Madgwick Filter Derivation: Gyroscope and Accelerometer (IMU) Form

This derivation targets a gimbal payload IMU. The result is the 6-DOF Madgwick update. There is no magnetometer, so yaw is gyro-only and will drift.

---

## 1. Conventions

- Quaternion **q** = [q₀, q₁, q₂, q₃]ᵀ, with q₀ as the scalar part. It is unit-norm.
- **q** describes the orientation of the Earth frame *E* relative to the sensor/payload frame *S* (Madgwick's ^S_E q).
- ⊗ is the Hamilton product and **q*** is the conjugate.
- Gyro measurement: **ω** = [0, ωₓ, ω_y, ω_z] in rad/s, in the sensor frame.
- Accelerometer measurement: **a** = [0, aₓ, a_y, a_z], normalised to unit length.
- Gravity reference in the Earth frame: **g** = [0, 0, 0, 1]. The accelerometer measures the reaction to gravity, so +Z points up.

---

## 2. Gyroscope Path: Quaternion Kinematics

The rate of change of orientation is

$$
\dot{\mathbf q}_{\omega} = \tfrac12\,\mathbf q \otimes \boldsymbol\omega .
$$

Expanded, this is

$$
\dot{\mathbf q}_{\omega} = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}.
$$

Integrating this alone accumulates gyro bias and noise into unbounded drift.

---

## 3. Accelerometer Path: Orientation as an Optimisation Problem

Find the **q** that rotates the known gravity direction into the measured direction:

$$
\min_{\mathbf q}\; \tfrac12\|\mathbf f(\mathbf q,\mathbf g,\mathbf a)\|^2,\qquad
\mathbf f = \mathbf q^*\otimes\mathbf g\otimes\mathbf q - \mathbf a .
$$

### 3.1 Objective function

Substituting **g** = [0, 0, 0, 1] and keeping the three vector components gives

$$
\mathbf f(\mathbf q,\mathbf a)=
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}.
$$

The first terms are the third row of the rotation matrix, i.e. the predicted gravity direction in the sensor frame.

### 3.2 Jacobian

Take the partial derivative of each component of **f** with respect to each of q₀ … q₃:

$$
\mathbf J(\mathbf q)=\frac{\partial \mathbf f}{\partial \mathbf q}=
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}.
$$

### 3.3 Gradient

$$
\nabla f = \mathbf J^{\mathsf T}\mathbf f.
$$

Writing f₁, f₂, f₃ for the three components:

$$
\nabla f=
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2\\
\;\;2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\;\;2q_1 f_1 + 2q_2 f_2
\end{bmatrix}.
$$

### 3.4 Gradient-descent step

$$
\mathbf q_{\nabla,t} = \hat{\mathbf q}_{t-1} - \mu_t\,\frac{\nabla f}{\|\nabla f\|}.
$$

A single iteration per sample is sufficient, provided μ_t is at least as large as the actual orientation change per sample.

---

## 4. Fusion of the Two Estimates

### 4.1 Weighted combination

The two estimates are blended as

$$
\hat{\mathbf q}_t = \gamma_t\,\mathbf q_{\nabla,t} + (1-\gamma_t)\,\mathbf q_{\omega,t},
\qquad
\mathbf q_{\omega,t} = \hat{\mathbf q}_{t-1} + \dot{\mathbf q}_{\omega,t}\Delta t .
$$

### 4.2 Choosing the step size and weight

Choose the gradient step size proportional to the orientation change per sample:

$$
\mu_t = \alpha\,\|\dot{\mathbf q}_{\omega,t}\|\,\Delta t, \qquad \alpha > 1.
$$

The optimal weight balances two rates. The gyro-path divergence is β, the gyro measurement-error magnitude. The gradient-path convergence is μ_t / Δt. Setting

$$
(1-\gamma_t)\,\beta = \gamma_t\,\frac{\mu_t}{\Delta t}
\quad\Rightarrow\quad
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}.
$$

### 4.3 Large-α approximation

Assume α is large, so μ_t is large and γ_t ≈ 0. Then:

- The q̂_{t−1} terms cancel to first order.
- γ_t μ_t / Δt → β.

The fused estimate simplifies to

$$
\hat{\mathbf q}_t \approx \hat{\mathbf q}_{t-1} + \Big(\dot{\mathbf q}_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\Big)\Delta t .
$$

### Final update equations

$$
\boxed{\;
\begin{aligned}
\dot{\hat{\mathbf q}}_{t} &= \tfrac12\,\hat{\mathbf q}_{t-1}\otimes\boldsymbol\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\\[2pt]
\hat{\mathbf q}_t &= \frac{\hat{\mathbf q}_{t-1} + \dot{\hat{\mathbf q}}_{t}\,\Delta t}{\big\|\hat{\mathbf q}_{t-1} + \dot{\hat{\mathbf q}}_{t}\,\Delta t\big\|}
\end{aligned}\;}
$$

The normalised gradient is a unit-magnitude correction applied at rate β. It removes only the error that the accelerometer can observe, which is roll and pitch.

---

## 5. Choosing β (and optional ζ)

Suppose each gyro axis has zero-mean error of magnitude ω̃_β. Map that error into quaternion-derivative space:

$$
\beta = \|\tfrac12\,\hat{\mathbf q}\otimes[0,\tilde\omega_\beta,\tilde\omega_\beta,\tilde\omega_\beta]\| = \sqrt{\tfrac34}\,\tilde\omega_\beta .
$$

- **Example:** a gyro error of 3°/s ≈ 0.052 rad/s gives β ≈ 0.045.
- **Tuning trade-off:** a larger β converges faster but passes through more accelerometer noise and linear-acceleration disturbance.

**Optional gyro-bias compensation** (Madgwick's ζ term):

$$
\boldsymbol\omega_{\epsilon} = 2\,\hat{\mathbf q}^*\otimes\frac{\nabla f}{\|\nabla f\|},\qquad
\mathbf b_t = \mathbf b_{t-1} + \zeta\,\boldsymbol\omega_\epsilon\,\Delta t,\qquad
\boldsymbol\omega_c = \boldsymbol\omega - \mathbf