# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

I don't know what "Cycle 24" refers to in your project, so this is a general derivation. Gimbal-specific considerations are in Section 7.

## 1. Notation and Conventions

- **q** = [q₀, q₁, q₂, q₃]ᵀ is the unit quaternion representing the orientation of the **earth frame relative to the sensor frame** (Madgwick's ^S_E q). q₀ is the scalar part.
- ⊗ denotes the quaternion (Hamilton) product, and q* denotes the conjugate.
- Gyroscope measurement: ω = [0, ω_x, ω_y, ω_z] in rad/s, sensor frame.
- Accelerometer measurement, normalized: â = [0, a_x, a_y, a_z], with ‖â‖ = 1.

## 2. Gyroscope Path: Quaternion Kinematics

The rate of change of orientation from angular velocity is:

$$
\dot{\mathbf q}_{\omega,t} = \tfrac{1}{2}\,\hat{\mathbf q}_{est,t-1} \otimes \boldsymbol\omega_t
$$

Expanded:

$$
\dot{\mathbf q}_\omega = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone gives accurate short-term orientation, but gyro bias and noise make it drift without bound.

## 3. Accelerometer Path: Orientation as an Optimization Problem

The goal is to find the q that rotates a known earth-frame reference direction **d** onto the measured sensor-frame direction **s**:

$$
\min_{\mathbf q}\; \tfrac12\|\mathbf f(\mathbf q,\mathbf d,\mathbf s)\|^2,\qquad
\mathbf f = \mathbf q^* \otimes \mathbf d \otimes \mathbf q - \mathbf s
$$

For gravity, the reference is **d** = [0, 0, 0, 1] and the measurement is **s** = â. Substituting gives:

$$
\mathbf f_g(\mathbf q,\hat{\mathbf a}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

The first three terms are the third column of the rotation matrix, i.e., gravity predicted in the sensor frame.

**Jacobian:**

$$
\mathbf J_g(\mathbf q) =
\begin{bmatrix}
-2q_2 & 2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & 2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

**Gradient of the cost:**

$$
\nabla f = \mathbf J_g^T(\hat{\mathbf q}_{est,t-1})\;\mathbf f_g(\hat{\mathbf q}_{est,t-1},\hat{\mathbf a}_t)
$$

Multiplying out gives closed-form components, which is how embedded implementations compute them:

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

One gradient-descent step per sample is used:

$$
\mathbf q_{\nabla,t} = \hat{\mathbf q}_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

## 4. Fusion

The two estimates are blended with weight γ:

$$
\hat{\mathbf q}_{est,t} = \gamma\,\mathbf q_{\nabla,t} + (1-\gamma)\,\mathbf q_{\omega,t}
$$

Madgwick chooses γ so that the divergence rate of the gradient estimate (μ/Δt) equals the convergence rate β of the gyro error. He then assumes μ is very large, which makes γ ≈ βΔt/μ ≪ 1. Under this assumption the fusion reduces to a single rate equation:

$$
\boxed{\;\dot{\hat{\mathbf q}}_{est,t} = \tfrac12\,\hat{\mathbf q}_{est,t-1}\otimes\boldsymbol\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

The gyroscope term drives the estimate, and the normalized accelerometer gradient pulls it back toward gravity at rate β.

**Discrete update:**

$$
\hat{\mathbf q}_{est,t} = \hat{\mathbf q}_{est,t-1} + \dot{\hat{\mathbf q}}_{est,t}\,\Delta t,
\qquad
\hat{\mathbf q}_{est,t} \leftarrow \frac{\hat{\mathbf q}_{est,t}}{\|\hat{\mathbf q}_{est,t}\|}
$$

## 5. Gain Selection

β is tied to the gyroscope measurement error. The error is a quaternion whose three vector components each carry rate error ω̃_β, giving a magnitude of √3/2 · ω̃_β:

$$
\beta = \sqrt{\tfrac34}\;\tilde\omega_\beta
$$

Here ω̃_β is the expected gyro error in rad/s, typically bias plus noise. Typical values of β are 0.03–0.1.

- **Larger β:** faster drift correction, but more sensitivity to linear acceleration.
- **Smaller β:** smoother output, but slower correction.

## 6. Per-Sample Algorithm

1. Read ω (rad/s) and **a**. If ‖**a**‖ ≈ 0, skip the correction and integrate the gyro only.
2. Normalize **a** to â.
3. Compute q̇_ω from Section 2.
4. Compute s₀…s₃, then normalize: **s** ← **s**/‖**s**‖.
5. Compute q̇ = q̇_ω − β**s**.
6. Integrate: q ← q + q̇Δt.
7. Normalize q.

## 7. Gimbal Payload Considerations

- **Yaw is unobservable.** Gravity constrains only roll and pitch, so the gradient's yaw component is zero and heading will drift. Fix this with a magnetometer (MARG variant, which adds an **f_b** term for Earth's magnetic field) or with motor encoders plus the base IMU heading.
- **Linear and centripetal acceleration corrupt â.** Gimbal slewing and vehicle maneuvers add acceleration beyond gravity. For an IMU offset by r from the rotation axis, the centripetal term is ω×(ω×r). Mitigations:
  - Gate or reduce β when |‖**a**‖ − g| exceeds a threshold, or when ‖ω‖ is large.
  - Compensate for the known lever arm.
  - Feed forward platform acceleration from the base IMU.
-