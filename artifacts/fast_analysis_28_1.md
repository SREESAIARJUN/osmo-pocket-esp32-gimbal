# Madgwick Filter Derivation: Gyroscope + Accelerometer (IMU) Orientation Update

I don't have context for what "Cycle 28" refers to in your project. Below is the general derivation, with gimbal-specific notes at the end.

---

## 1. Conventions

- **Quaternion:** $\hat{q} = {}^S_E\hat{q} = [q_0\; q_1\; q_2\; q_3]^T$ (scalar first, unit norm). It describes the orientation of the earth frame *E* relative to the sensor frame *S*, which here is the payload IMU frame.
- **Gyroscope** (rad/s, sensor frame): ${}^S\omega = [0\; g_x\; g_y\; g_z]$
- **Accelerometer** (normalized): ${}^S\hat{a} = [0\; a_x\; a_y\; a_z]$, with $\|a\| = 1$
- **Quaternion product:** $\otimes$ (Hamilton). Conjugate: $q^* = [q_0, -q_1, -q_2, -q_3]$

---

## 2. Orientation from the Gyroscope (Rate Integration)

Quaternion kinematics give the rate of change of orientation:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\, {}^S_E\hat{q}_{est,t-1} \otimes {}^S\omega_t
$$

Expanded:

$$
\begin{aligned}
\dot q_{\omega,0} &= \tfrac12(-q_1 g_x - q_2 g_y - q_3 g_z)\\
\dot q_{\omega,1} &= \tfrac12(\;\;q_0 g_x + q_2 g_z - q_3 g_y)\\
\dot q_{\omega,2} &= \tfrac12(\;\;q_0 g_y - q_1 g_z + q_3 g_x)\\
\dot q_{\omega,3} &= \tfrac12(\;\;q_0 g_z + q_1 g_y - q_2 g_x)
\end{aligned}
$$

Integrating this alone drifts, because gyro bias and noise accumulate without bound.

---

## 3. Orientation from the Accelerometer (Optimization Problem)

We seek the $\hat q$ that rotates a known earth-frame reference direction ${}^E\hat d$ onto the measured sensor-frame direction ${}^S\hat s$.

**Objective function:**

$$
f({}^S_E\hat q, {}^E\hat d, {}^S\hat s) = \hat q^* \otimes {}^E\hat d \otimes \hat q - {}^S\hat s
$$

$$
\min_{\hat q \in \mathbb{R}^4} \tfrac12 \|f\|^2
$$

**Gravity case.** Use ${}^E\hat g = [0\;0\;0\;1]$ and ${}^S\hat s = {}^S\hat a$. Carrying out the rotation gives the third column of the rotation matrix, so:

$$
f_g(\hat q, {}^S\hat a) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

**Jacobian** $J_g = \partial f_g / \partial \hat q$:

$$
J_g(\hat q) =
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

**Gradient of the cost:**

$$
\nabla f = J_g^T f_g
$$

Written out, with $f_g = [f_1, f_2, f_3]^T$:

$$
\begin{aligned}
\nabla f_0 &= -2q_2 f_1 + 2q_1 f_2\\
\nabla f_1 &= \;\;2q_3 f_1 + 2q_0 f_2 - 4q_1 f_3\\
\nabla f_2 &= -2q_0 f_1 + 2q_3 f_2 - 4q_2 f_3\\
\nabla f_3 &= \;\;2q_1 f_1 + 2q_2 f_2
\end{aligned}
$$

**Gradient descent step:**

$$
\hat q_{\nabla,t} = \hat q_{est,t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion

The step size $\mu_t$ must be large enough that the gradient-descent convergence rate is at least the physical rate of orientation change. Madgwick sets

$$
\mu_t = \alpha \|\dot q_{\omega,t}\| \Delta t, \qquad \alpha > 1.
$$

So only **one gradient step per sample** is needed.

The two estimates are then blended:

$$
\hat q_{est,t} = \gamma\, \hat q_{\nabla,t} + (1-\gamma)\, \hat q_{\omega,t}
$$

The optimal weight $\gamma$ makes the gyro-error divergence rate $\beta$ equal to the accelerometer convergence rate $\mu_t/\Delta t$:

$$
(1-\gamma)\beta = \gamma \frac{\mu_t}{\Delta t}
\;\;\Rightarrow\;\;
\gamma = \frac{\beta}{\mu_t/\Delta t + \beta}
$$

Assuming $\alpha$ is very large, $\mu_t$ becomes large and $\gamma \approx \beta\Delta t/\mu_t \approx 0$. In this limit, $\hat q_{\nabla}$ reduces to $\hat q_{est,t-1} - \mu_t \nabla f/\|\nabla f\|$, the $\mu_t$ terms cancel, and the fusion collapses to a simple rate equation.

### Final Update Equations

$$
\boxed{\;\dot q_{est,t} = \tfrac12\, \hat q_{est,t-1} \otimes {}^S\omega_t \;-\; \beta \frac{\nabla f}{\|\nabla f\|}\;}
$$

$$
\boxed{\;q_{est,t} = \hat q_{est,t-1} + \dot q_{est,t}\,\Delta t, \qquad \hat q_{est,t} = \frac{q_{est,t}}{\|q_{est,t}\|}\;}
$$

**Interpretation:** this is a gyro-driven integrator with a normalized, accelerometer-derived correction applied along the error direction, at a rate $\beta$.

---

## 5. Choosing β

Treat $\beta$ as the magnitude of the quaternion-derivative error caused by gyro measurement error $\tilde\omega_\beta$ (rad/s), assumed equal on all three axes:

$$
\beta = \left\| \tfrac12 \hat q \otimes [0\;\tilde\omega_\beta\;\tilde\omega_\beta\;\tilde\omega_\beta] \right\| = \sqrt{\tfrac34}\,\tilde\omega_\beta
$$

For example, gyro error of $\sim 5^\circ/\text{s}$ gives $\beta \approx 0.075$. Typical tuned values for gimbals fall in the range 0.01–0.1.

**Optional gyro-bias compensation** (from Madgwick's MARG extension, also applicable here):

$$
\omega_{\varepsilon} = 2\hat q^* \otimes \frac{\nabla f}{\|\nabla f\|}, \qquad
\omega_b = \zeta \sum \omega_\varepsilon \Delta t, \qquad
\omega_c = \omega - \omega_b
$$

Here $\zeta = \sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the bias drift rate.

---

## 6. Implementation Pseudocode

```
function madgwick_update(q, g, a, beta, dt):
    qdot = 0.5 * quat_mult(q