# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU Variant)

Your project-specific context (the gimbal payload and "Cycle 14") isn't something I have details on. This derivation is the general 6-DOF formulation, written for an IMU rigidly mounted on the payload. Plug in your own sample rate and sensor noise figures.

---

## 1. Notation and Conventions

- The quaternion ${}^S_E\hat{q} = [q_0,\ q_1,\ q_2,\ q_3]^T$ (scalar first) describes the orientation of the **Earth frame E relative to the sensor frame S**. It has unit norm.
- $\otimes$ is the Hamilton quaternion product. $\hat{q}^*$ is the conjugate.
- The gyroscope gives ${}^S\omega = [0,\ \omega_x,\ \omega_y,\ \omega_z]$ in rad/s, in the payload sensor frame.
- The accelerometer gives ${}^S\hat{a} = [0,\ a_x,\ a_y,\ a_z]$, normalized to unit length.
- Gravity in the Earth frame is ${}^E\hat{g} = [0,\ 0,\ 0,\ 1]$ (NED/ENU sign conventions only flip signs).

---

## 2. Gyroscope Path: Quaternion Kinematics

The time derivative of orientation due to the angular rate is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\,{}^S_E\hat{q}_{t-1} \otimes {}^S\omega_t
$$

Expanding the product gives:

$$
\dot{q}_\omega = \frac{1}{2}
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z \\
\ \ q_0\omega_x + q_2\omega_z - q_3\omega_y \\
\ \ q_0\omega_y - q_1\omega_z + q_3\omega_x \\
\ \ q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone, $q_t = q_{t-1} + \dot{q}_\omega \Delta t$, accumulates drift from gyro bias and noise. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Path: Orientation as an Optimization Problem

We seek the orientation $\hat{q}$ that rotates the known gravity reference into the measured direction. This is posed as a minimization:

$$
\min_{\hat{q}\in\mathbb{R}^4} f(\hat{q}, {}^E\hat{g}, {}^S\hat{a}), \qquad
f = \hat{q}^* \otimes {}^E\hat{g} \otimes \hat{q} - {}^S\hat{a}
$$

### 3.1 Objective Function
With ${}^E\hat{g} = [0,0,0,1]$, the rotated gravity vector minus the measurement is:

$$
f_g(\hat{q}, {}^S\hat{a}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x \\
2(q_0q_1 + q_2q_3) - a_y \\
2(\tfrac{1}{2} - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### 3.2 Jacobian
Differentiating each row with respect to $[q_0, q_1, q_2, q_3]$:

$$
J_g(\hat{q}) =
\begin{bmatrix}
-2q_2 & \ \ 2q_3 & -2q_0 & 2q_1 \\
\ \ 2q_1 & \ \ 2q_0 & \ \ 2q_3 & 2q_2 \\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### 3.3 Gradient
For the cost $\tfrac{1}{2}\|f\|^2$:

$$
\nabla f = J_g^T(\hat{q})\, f_g(\hat{q}, {}^S\hat{a})
$$

Expanding, and simplifying with $\|q\| = 1$, gives the form used in Madgwick's reference code:

$$
\nabla f =
\begin{bmatrix}
4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y \\
4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z \\
4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_1^2q_2 + 8q_2^3 + 4q_2a_z \\
4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{bmatrix}
$$

As a check, the first row follows directly: $-2q_2 f_1 + 2q_1 f_2 = 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y$.

### 3.4 Gradient-Descent Step
A single gradient-descent iteration per sample gives:

$$
q_{\nabla,t} = \hat{q}_{t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

Madgwick chooses the step size to keep up with the physical rotation rate:

$$
\mu_t = \alpha\,\|\dot{q}_{\omega,t}\|\,\Delta t, \qquad \alpha > 1
$$

---

## 4. Fusion

The two estimates are blended as a complementary combination:

$$
\hat{q}_{est,t} = \gamma_t\, q_{\nabla,t} + (1-\gamma_t)\, q_{\omega,t}, \qquad 0 \le \gamma_t \le 1
$$

The optimal weight balances the convergence rate of $q_\nabla$ against the divergence rate of $q_\omega$:

$$
(1-\gamma_t)\beta = \gamma_t \frac{\mu_t}{\Delta t}
\;\;\Rightarrow\;\;
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

Here $\beta$ is the divergence rate of the gyro estimate, expressed as a quaternion rate.

Assume $\alpha$, and therefore $\mu_t$, is very large. Then:
- $\gamma_t \approx \beta\Delta t / \mu_t \to$ small.
- The $\hat{q}_{t-1}$ term inside $q_\nabla$ becomes negligible relative to the scaled gradient.

Substituting and simplifying gives the compact Madgwick update.

### Final Rate Equation

$$
\boxed{\;{}^S_E\dot{q}_{est,t} = {}^S_E\dot{q}_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

### Integration and Normalization

$$
\boxed{\;\hat{q}_{est,t} = \hat{q}_{est,t-1} + \dot{q}_{est,t}\,\Delta t, \qquad \hat{q}_{est,t} \leftarrow \frac{\hat{q}_{est,t}}{\|\hat{q}_{est,t}\|}\;}
$$

**Interpretation:** the gyroscope drives the orientation forward. The normalized gradient is a unit-magnitude correction pulling the estimate toward agreement with gravity. $\beta$ sets how hard that correction pulls.

---

## 5. Choosing β

If each gyro axis has zero-mean measurement error $\tilde{\omega}_\beta$ (rad/s), then:

$$
\beta = \sqrt{\tfrac{3}{4}}\,\tilde{\omega}_\beta
$$

Example: a gyro error of 5 °/s ≈ 0.087 rad/s gives β ≈ 0.075. Typical tuned values are 0.03–0.1.

Optional gyro-bias compensation (from Madgwick's MARG extension, also usable for IMU only):

$$
\om