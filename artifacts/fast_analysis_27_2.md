# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Form)

I don't have the specifics of your "Cycle 27" configuration, so this derivation is general. The gimbal-specific notes at the end cover the parts that usually matter on a payload.

---

## 1. Conventions

- The quaternion is $\hat{q} = {}^S_E\hat{q} = [q_1\; q_2\; q_3\; q_4]^T$, with $q_1$ as the scalar part. It describes the **Earth frame relative to the sensor frame**.
- $\otimes$ denotes the Hamilton product. $\hat{q}^*$ is the conjugate.
- Vectors are embedded as pure quaternions, e.g. $\boldsymbol{\omega} \to [0\; \omega_x\; \omega_y\; \omega_z]$.
- The constraint $\|\hat{q}\| = 1$ is enforced after every step.

---

## 2. Gyroscope Path: Quaternion Kinematics

The angular rate measured in the sensor frame is ${}^S\boldsymbol{\omega} = [0\; \omega_x\; \omega_y\; \omega_z]$. The rate of change of orientation is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\, {}^S_E\hat{q}_{est,t-1} \otimes {}^S\boldsymbol{\omega}_t
$$

Expanded:

$$
\dot{q}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_2\omega_x - q_3\omega_y - q_4\omega_z \\
\;\;q_1\omega_x + q_3\omega_z - q_4\omega_y \\
\;\;q_1\omega_y - q_2\omega_z + q_4\omega_x \\
\;\;q_1\omega_z + q_2\omega_y - q_3\omega_x
\end{bmatrix}
$$

Integrating this alone gives an orientation that drifts without bound because of gyro bias and noise.

---

## 3. Accelerometer Path: Orientation as an Optimization Problem

### 3.1 Objective function

Find the $\hat{q}$ that rotates a known Earth-frame reference direction ${}^E\hat{d}$ onto the measured sensor-frame direction ${}^S\hat{s}$:

$$
\min_{\hat{q}\in\mathbb{R}^4} \tfrac12\|\mathbf{f}\|^2,\qquad
\mathbf{f}(\hat{q},{}^E\hat{d},{}^S\hat{s}) = \hat{q}^*\otimes{}^E\hat{d}\otimes\hat{q} - {}^S\hat{s}
$$

### 3.2 Specialize to gravity

- The reference direction is gravity: ${}^E\hat{g} = [0\;0\;0\;1]$.
- The measurement is the normalized acceleration: ${}^S\hat{a} = [0\;a_x\;a_y\;a_z]/\|\mathbf a\|$.

Evaluating the rotation $\hat{q}^*\otimes[0\,0\,0\,1]\otimes\hat{q}$ gives the predicted gravity direction in the sensor frame. Subtracting the measurement yields the residual:

$$
\mathbf{f}_g(\hat{q},{}^S\hat{a}) =
\begin{bmatrix}
2(q_2q_4 - q_1q_3) - a_x \\
2(q_1q_2 + q_3q_4) - a_y \\
2(\tfrac12 - q_2^2 - q_3^2) - a_z
\end{bmatrix}
$$

### 3.3 Jacobian

Differentiate $\mathbf{f}_g$ with respect to $[q_1, q_2, q_3, q_4]$:

$$
\mathbf{J}_g(\hat{q}) =
\begin{bmatrix}
-2q_3 & \;\;2q_4 & -2q_1 & 2q_2 \\
\;\;2q_2 & \;\;2q_1 & \;\;2q_4 & 2q_3 \\
0 & -4q_2 & -4q_3 & 0
\end{bmatrix}
$$

### 3.4 Gradient

The gradient of $\tfrac12\|\mathbf f\|^2$ is

$$
\nabla \mathbf{f} = \mathbf{J}_g^T(\hat{q}_{est,t-1})\,\mathbf{f}_g(\hat{q}_{est,t-1},{}^S\hat{a}_t)
$$

A pure gradient-descent estimate would then be:

$$
\hat{q}_{\nabla,t} = \hat{q}_{est,t-1} - \mu_t\frac{\nabla\mathbf f}{\|\nabla\mathbf f\|}
$$

**Choosing the step size.** Madgwick picks $\mu_t = \alpha\|\dot{q}_{\omega,t}\|\Delta t$ with $\alpha > 1$. This makes the convergence rate at least as fast as the physical rotation rate, so a single gradient step per sample is sufficient.

---

## 4. Fusion

### 4.1 Complementary weighting

Blend the two estimates:

$$
\hat{q}_{est,t} = \gamma_t\,\hat{q}_{\nabla,t} + (1-\gamma_t)\,\hat{q}_{\omega,t}
$$

Choose $\gamma_t$ so that the divergence rate of $\hat q_\omega$ (set by $\beta$) balances the convergence rate of $\hat q_\nabla$ ($\mu_t/\Delta t$):

$$
(1-\gamma_t)\beta = \gamma_t\frac{\mu_t}{\Delta t}\;\Rightarrow\;\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

### 4.2 Simplification

Because $\mu_t$ is large (since $\alpha$ is large), $\gamma_t \approx 0$ and $\gamma_t\mu_t/\Delta t \approx \beta$. Substituting these into the blend collapses it to a single rate equation.

### 4.3 Final update equations

$$
\boxed{\;\dot{q}_{est,t} = \tfrac12\,\hat{q}_{est,t-1}\otimes{}^S\boldsymbol\omega_t \;-\; \beta\,\frac{\nabla\mathbf f}{\|\nabla\mathbf f\|}\;}
$$

$$
\boxed{\;\hat{q}_{est,t} = \frac{\hat{q}_{est,t-1} + \dot{q}_{est,t}\Delta t}{\left\|\hat{q}_{est,t-1} + \dot{q}_{est,t}\Delta t\right\|}\;}
$$

**Interpretation:** the gyroscope integrates orientation forward, and the accelerometer applies a corrective term of fixed magnitude $\beta$ that pulls the estimate along the steepest-descent direction toward gravity alignment.

---

## 5. Choosing β

Model the gyro error as zero-mean with magnitude $\tilde\omega_\beta$ on each axis, i.e. $\tilde{\boldsymbol\omega}=[0\;\tilde\omega_\beta\;\tilde\omega_\beta\;\tilde\omega_\beta]$. The resulting quaternion-rate error magnitude is:

$$
\beta = \left\|\tfrac12\hat q\otimes\tilde{\boldsymbol\omega}\right\| = \sqrt{\tfrac34}\,\tilde\omega_\beta
$$

**Example:** for a gyro error of 3°/s, $\beta \approx 0.866 \times 0.0524 \approx 0.045$.

**Tuning trade-off:**
- Larger $\beta$ gives faster convergence and better drift rejection, but lets more accelerometer noise and vibration through.
- Smaller $\beta$ gives a smoother output, but the estimate drifts and is more sensitive to gyro bias.

---

## 6. Optional: Gyro Bias Compensation

Recover the angular-rate error implied by the correction direction:

$$
{}^S\boldsymbol\omega_{\epsilon,t} = 2\,\hat{q}^*_{est,t-1}\otimes\frac{\nabla\mathbf f}{\|\nabla\mathbf f