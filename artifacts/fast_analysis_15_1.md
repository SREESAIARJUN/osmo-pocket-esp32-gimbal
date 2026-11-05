# Madgwick Filter: Gyroscope and Accelerometer Orientation Update

I don't know the specifics of your "Cycle 15" requirements, such as the IMU part, sample rate or gimbal axis configuration. The derivation below is general. Section 8 lists the gimbal-specific considerations you should check against your design.

---

## 1. Conventions

- Quaternion: $\mathbf{q} = {}^S_E\hat{\mathbf{q}} = [q_0\;q_1\;q_2\;q_3]^T$ (scalar first). It describes the orientation of the Earth frame *E* relative to the sensor/payload frame *S*.
- Vector rotation from E to S: ${}^S\mathbf{v} = \mathbf{q}^* \otimes {}^E\mathbf{v} \otimes \mathbf{q}$, where vectors are written as pure quaternions $[0\;v_x\;v_y\;v_z]$.
- Quaternion product:

$$
\mathbf{a}\otimes\mathbf{b} =
\begin{bmatrix}
a_0b_0 - a_1b_1 - a_2b_2 - a_3b_3\\
a_0b_1 + a_1b_0 + a_2b_3 - a_3b_2\\
a_0b_2 - a_1b_3 + a_2b_0 + a_3b_1\\
a_0b_3 + a_1b_2 - a_2b_1 + a_3b_0
\end{bmatrix}
$$

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The gyro measures the angular rate in the sensor frame, ${}^S\boldsymbol{\omega} = [0\;\omega_x\;\omega_y\;\omega_z]$ in rad/s. The rate of change of orientation is:

$$
\dot{\mathbf{q}}_{\omega} = \tfrac12\,\mathbf{q}\otimes{}^S\boldsymbol{\omega}
$$

Expanded:

$$
\dot{\mathbf{q}}_{\omega} = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;\,q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;\,q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;\,q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone would accumulate drift from gyro bias and noise. The accelerometer is used to correct roll and pitch.

---

## 3. Accelerometer Correction as an Optimisation Problem

The goal is to find the $\mathbf{q}$ that rotates a known Earth-frame reference direction ${}^E\hat{\mathbf{d}}$ onto the measured sensor-frame direction ${}^S\hat{\mathbf{s}}$:

$$
\min_{\mathbf{q}}\;\tfrac12\|\mathbf{f}(\mathbf{q},{}^E\hat{\mathbf{d}},{}^S\hat{\mathbf{s}})\|^2,\qquad
\mathbf{f} = \mathbf{q}^*\otimes{}^E\hat{\mathbf{d}}\otimes\mathbf{q} - {}^S\hat{\mathbf{s}}
$$

For gravity, the inputs are:
- Reference direction: ${}^E\hat{\mathbf{g}} = [0\;0\;0\;1]$.
- Normalised accelerometer reading: ${}^S\hat{\mathbf{a}} = [0\;a_x\;a_y\;a_z]/\|\mathbf{a}\|$.

Evaluating the triple product, which selects the third column of the rotation matrix, gives the objective function:

$$
\mathbf{f}_g(\mathbf{q},{}^S\hat{\mathbf{a}}) =
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

Its Jacobian is:

$$
\mathbf{J}_g(\mathbf{q}) = \frac{\partial \mathbf{f}_g}{\partial \mathbf{q}} =
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

The gradient of the cost function is:

$$
\nabla f = \mathbf{J}_g^T(\mathbf{q})\,\mathbf{f}_g(\mathbf{q},{}^S\hat{\mathbf{a}})
$$

Expanding $\mathbf{J}_g^T\mathbf{f}_g$ gives the computationally cheap form used in Madgwick's reference code. Here $\mathbf{q}$ has unit norm and $\hat{\mathbf a}$ is normalised:

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

As a check, the first row is $s_0 = -2q_2 f_1 + 2q_1 f_2$. The cross terms $\pm4q_1q_2q_3$ cancel, leaving the expression above.

---

## 4. Gradient-Descent Step

A steepest-descent iteration on the cost function is:

$$
\mathbf{q}_{k+1} = \mathbf{q}_k - \mu\,\frac{\nabla f}{\|\nabla f\|}
$$

Madgwick's key insight is that **one iteration per sample is enough**, provided the step size $\mu$ is at least the orientation change that the gyro produces per sample:

$$
\mu_t = \alpha\,\|\dot{\mathbf{q}}_{\omega,t}\|\,\Delta t,\qquad \alpha > 1
$$

---

## 5. Fusion

The gyro estimate $\mathbf{q}_{\omega,t}$ and the gradient estimate $\mathbf{q}_{\nabla,t}$ are combined linearly:

$$
\mathbf{q}_{est,t} = \gamma_t\,\mathbf{q}_{\nabla,t} + (1-\gamma_t)\,\mathbf{q}_{\omega,t}
$$

The weight $\gamma_t$ is chosen so that the divergence rate of $\mathbf{q}_\omega$, set by the gyro error $\beta$, balances the convergence rate of $\mathbf{q}_\nabla$, which is $\mu_t/\Delta t$:

$$
(1-\gamma_t)\beta = \gamma_t\frac{\mu_t}{\Delta t}
\;\Rightarrow\;
\gamma_t = \frac{\beta}{\mu_t/\Delta t + \beta}
$$