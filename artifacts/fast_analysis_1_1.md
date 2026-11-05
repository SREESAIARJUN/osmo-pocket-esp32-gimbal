# Madgwick Filter Derivation (Gyroscope + Accelerometer, IMU Variant)

This derivation targets a gimbal payload IMU. It covers the full update equations and then works through the first filter cycle (Cycle 1) with numbers.

---

## 1. Notation and Conventions

| Symbol | Meaning |
|---|---|
| $\hat{q} = [q_0\; q_1\; q_2\; q_3]^T$ | Orientation quaternion $^S_E\hat{q}$ (Earth frame relative to sensor frame), scalar first |
| $^S\omega = [0\; \omega_x\; \omega_y\; \omega_z]$ | Gyro rate in rad/s, as a pure quaternion |
| $^S\hat{a} = [0\; a_x\; a_y\; a_z]$ | **Normalized** accelerometer reading |
| $^E\hat{g} = [0\; 0\; 0\; 1]$ | Gravity reference direction in the Earth frame |
| $\otimes$ | Hamilton quaternion product |
| $\hat{q}^*$ | Conjugate $[q_0, -q_1, -q_2, -q_3]$ |
| $\Delta t$ | Sample period |

---

## 2. Gyroscope Path: Quaternion Kinematics

The rate of change of orientation from the angular rate is:

$$
^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\, ^S_E\hat{q}_{est,t-1} \otimes {}^S\omega_t
$$

Expanding the product gives the explicit form:

$$
\dot{q}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z \\
\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y \\
\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x \\
\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone, $q_{\omega,t} = q_{est,t-1} + \dot{q}_\omega \Delta t$, accumulates gyro bias and noise into drift. The accelerometer path corrects that drift.

---

## 3. Accelerometer Path: Orientation as an Optimization Problem

### 3.1 Objective function

We want the orientation $\hat{q}$ that rotates the Earth gravity reference into the measured sensor direction:

$$
\min_{\hat{q}\in\mathbb{R}^4} \tfrac12\|f(\hat{q}, {}^E\hat{g}, {}^S\hat{a})\|^2,
\qquad
f = \hat{q}^* \otimes {}^E\hat{g} \otimes \hat{q} - {}^S\hat{a}
$$

### 3.2 Expanding with $^E\hat{g} = [0,0,0,1]$

Rotating $\hat{z}$ by the quaternion gives the third column of the rotation matrix:

$$
f_g(\hat{q}, {}^S\hat{a}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x \\
2(q_0q_1 + q_2q_3) - a_y \\
2\left(\tfrac12 - q_1^2 - q_2^2\right) - a_z
\end{bmatrix}
\equiv
\begin{bmatrix} f_1\\ f_2\\ f_3\end{bmatrix}
$$

### 3.3 Jacobian

Differentiate each row with respect to $q_0 \dots q_3$:

$$
J_g(\hat{q}) = \frac{\partial f_g}{\partial \hat q}=
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & \;\;2q_1 \\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & \;\;2q_2 \\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 3.4 Gradient

The gradient of the cost $\tfrac12 f^Tf$ is $\nabla f = J_g^T f_g$:

$$
\nabla f =
\begin{bmatrix}
-2q_2 f_1 + 2q_1 f_2 \\
\;\;2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3 \\
-2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3 \\
\;\;2q_1 f_1 + 2q_2 f_2
\end{bmatrix}
$$

### 3.5 Gradient-descent step

One gradient-descent step on the accelerometer cost is:

$$
q_{\nabla,t} = \hat{q}_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick showed that a single step per sample is sufficient, provided the step size satisfies:

$$
\mu_t = \alpha\,\|\dot{q}_{\omega,t}\|\,\Delta t, \qquad \alpha > 1
$$

With this choice, convergence keeps pace with the physical rotation rate.

---

## 4. Fusion: Deriving the β Form

### 4.1 Complementary weighting

Fuse the two estimates with a weight $\gamma_t$:

$$
q_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}
$$

The optimal $\gamma_t$ balances the gyro divergence rate $\beta$ against the gradient convergence rate $\mu_t/\Delta t$:

$$
(1-\gamma_t)\beta = \gamma_t \frac{\mu_t}{\Delta t}
\;\;\Rightarrow\;\;
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

### 4.2 Large-α simplification

Because $\alpha$ is large, $\mu_t$ is large. This has two consequences:

- $\gamma_t \approx \beta\Delta t/\mu_t \to$ small.
- The $q_{est,t-1}$ term inside $q_{\nabla}$ cancels with the one inside $q_\omega$.

Substituting and simplifying gives:

$$
q_{est,t} \approx \hat{q}_{est,t-1} + \left(\dot{q}_{\omega,t} - \beta\frac{\nabla f}{\|\nabla f\|}\right)\Delta t
$$

### 4.3 Final Madgwick update equations

$$
\boxed{
\begin{aligned}
\dot{q}_{est,t} &= \tfrac12\, \hat{q}_{est,t-1}\otimes {}^S\omega_t \;-\; \beta\,\frac{J_g^T f_g}{\|J_g^T f_g\|} \\[4pt]
q_{est,t} &= \hat{q}_{est,t-1} + \dot{q}_{est,t}\,\Delta t \\[4pt]