# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

I don't have the context of your earlier cycles, so this derivation is self-contained. It uses the standard Madgwick conventions. Where a gimbal payload changes things, I flag it in Section 8.

---

## 1. Conventions

- The orientation quaternion $\hat{q} = {}^S_E\hat{q} = [q_0\; q_1\; q_2\; q_3]^T$ (scalar first) describes the Earth frame $E$ relative to the sensor frame $S$.
- The gyroscope measurement is ${}^S\omega = [0\;\omega_x\;\omega_y\;\omega_z]^T$, in rad/s, written as a pure quaternion.
- The accelerometer measurement is ${}^S a = [0\;a_x\;a_y\;a_z]^T$, normalized to unit length.
- Earth gravity reference: ${}^E g = [0\;0\;0\;1]^T$. This assumes NED/ENU with the z-axis along the gravity reaction, which is what a static accelerometer reads.
- $\otimes$ denotes the Hamilton product and $\hat{q}^*$ the conjugate.

---

## 2. Gyroscope Propagation (Kinematics)

The rate of change of orientation is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\,{}^S_E\hat{q}_{t-1} \otimes {}^S\omega_t
$$

Expanded:

$$
\dot{q}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z \\
\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y \\
\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x \\
\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone drifts, because gyro bias and noise accumulate. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

We want the orientation that rotates the Earth gravity reference into the measured sensor direction:

$$
\min_{\hat{q}} \; f(\hat{q}, {}^E d, {}^S s) = \hat{q}^* \otimes {}^E d \otimes \hat{q} - {}^S s
$$

### 3.1 Objective function for gravity

Substituting ${}^E d = [0,0,0,1]$ and ${}^S s = {}^S a$:

$$
f_g(\hat{q}, {}^S a) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x \\
2(q_0q_1 + q_2q_3) - a_y \\
2(\tfrac{1}{2} - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

The first three terms are the third column of the rotation matrix, i.e. gravity expressed in the sensor frame.

### 3.2 Jacobian

Taking partial derivatives with respect to $q_0, q_1, q_2, q_3$:

$$
J_g(\hat{q}) =
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1 \\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2 \\
\;\;0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 3.3 Gradient

$$
\nabla f = J_g^T(\hat{q}_{t-1})\, f_g(\hat{q}_{t-1}, {}^S a_t)
$$

Fully expanded, using $\|q\|=1$ to simplify (this is Madgwick's reference form):

$$
\nabla f =
\begin{bmatrix}
4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y \\
4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z \\
4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z \\
4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{bmatrix}
$$

(These are proportional to $J^Tf$. A constant scale factor is irrelevant because the gradient is normalized next.)

### 3.4 One gradient-descent step per sample

$$
\hat{q}_{\nabla,t} = \hat{q}_{t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick shows that one step per sample is sufficient if the step size satisfies $\mu_t = \alpha\|\dot{q}_{\omega,t}\|\Delta t$ with $\alpha > 1$. In other words, the correction converges at least as fast as the orientation changes.

---

## 4. Fusion

### 4.1 Complementary weighting

Combine the two estimates:

$$
\hat{q}_{est,t} = \gamma_t\, \hat{q}_{\nabla,t} + (1-\gamma_t)\, \hat{q}_{\omega,t}, \qquad \hat{q}_{\omega,t} = \hat{q}_{t-1} + \dot{q}_{\omega,t}\Delta t
$$

### 4.2 Choosing the optimal weight

The optimal $\gamma_t$ balances the divergence rate of the gyro estimate ($\beta$) against the convergence rate of the gradient step ($\mu_t/\Delta t$):

$$
(1-\gamma_t)\beta = \gamma_t \frac{\mu_t}{\Delta t}
\;\;\Rightarrow\;\;
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

### 4.3 Limit of large step size

Because $\mu_t$ is assumed very large ($\alpha \gg 1$), $\gamma_t \approx \beta\Delta t/\mu_t \to 0$. The $\hat{q}_{t-1}$ terms then cancel, and the fusion collapses to a single corrected rate:

$$
\boxed{\;\dot{q}_{est,t} = \tfrac{1}{2}\,\hat{q}_{t-1}\otimes{}^S\omega_t \;-\; \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

### 4.4 Discrete update

Integrate, then renormalize:

$$
\boxed{\;\hat{q}_t = \hat{q}_{t-1} + \dot{q}_{est,t}\,\Delta t, \qquad \hat{q}_t \leftarrow \frac{\hat{q}_t}{\|\hat{q}_t\|}\;}
$$

---

## 5. Filter Gain β

$\beta$ represents the expected gyroscope error expressed as a quaternion rate. Assume a zero