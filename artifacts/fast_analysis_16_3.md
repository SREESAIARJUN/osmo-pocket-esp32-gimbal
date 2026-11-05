# Madgwick Filter Derivation (Gyroscope + Accelerometer, IMU Variant)

I don't have the context for "Cycle 16" in your project, so this is the general derivation, followed by notes specific to gimbal payloads. If Cycle 16 uses particular frame conventions, sample rates or sensor specs, the parameter choices in Sections 5 and 6 are where those would plug in.

---

## 1. Conventions

- The orientation quaternion $\hat{q} = {}^S_E\hat{q} = [q_0\; q_1\; q_2\; q_3]^T$ (scalar first) describes the Earth frame $E$ relative to the sensor (payload) frame $S$. It is unit norm.
- Gyroscope reading: ${}^S\omega = [0\;\omega_x\;\omega_y\;\omega_z]$ in rad/s.
- Accelerometer reading, normalized: ${}^S\hat{a} = [0\;a_x\;a_y\;a_z]$, with $\|a\| = 1$.
- Gravity reference in the Earth frame: ${}^E\hat{g} = [0\;0\;0\;1]$.

---

## 2. Gyroscope Propagation (Quaternion Kinematics)

The rate of change of orientation is:

$$
{}^S_E\dot{q}_{\omega,t} = \tfrac{1}{2}\, {}^S_E\hat{q}_{t-1} \otimes {}^S\omega_t
$$

Expanded:

$$
\dot{q}_\omega = \tfrac12
\begin{bmatrix}
-q_1\omega_x - q_2\omega_y - q_3\omega_z\\
\;\;\, q_0\omega_x + q_2\omega_z - q_3\omega_y\\
\;\;\, q_0\omega_y - q_1\omega_z + q_3\omega_x\\
\;\;\, q_0\omega_z + q_1\omega_y - q_2\omega_x
\end{bmatrix}
$$

Integrating this alone accumulates drift. The accelerometer corrects roll and pitch.

---

## 3. Accelerometer Correction as an Optimization Problem

We look for the orientation that rotates the known gravity reference into the measured direction:

$$
\min_{q}\; f(q,\,{}^E\hat g,\,{}^S\hat a), \qquad
f = \hat q^* \otimes {}^E\hat g \otimes \hat q - {}^S\hat a
$$

With ${}^E\hat g = [0\,0\,0\,1]$, the objective reduces to three components:

$$
f_g(q,\hat a)=
\begin{bmatrix}
2(q_1q_3 - q_0q_2) - a_x\\
2(q_0q_1 + q_2q_3) - a_y\\
2(\tfrac12 - q_1^2 - q_2^2) - a_z
\end{bmatrix}
$$

### Jacobian

$$
J_g(q)=\frac{\partial f_g}{\partial q}=
\begin{bmatrix}
-2q_2 & \;\;2q_3 & -2q_0 & 2q_1\\
\;\;2q_1 & \;\;2q_0 & \;\;2q_3 & 2q_2\\
0 & -4q_1 & -4q_2 & 0
\end{bmatrix}
$$

### Gradient

$$
\nabla f = J_g^T f_g
$$

Expanding $\nabla f$ and simplifying with the unit-norm constraint gives Madgwick's efficient form:

$$
\begin{aligned}
s_0 &= 4q_0q_2^2 + 2q_2a_x + 4q_0q_1^2 - 2q_1a_y\\
s_1 &= 4q_1q_3^2 - 2q_3a_x + 4q_0^2q_1 - 2q_0a_y - 4q_1 + 8q_1^3 + 8q_1q_2^2 + 4q_1a_z\\
s_2 &= 4q_0^2q_2 + 2q_0a_x + 4q_2q_3^2 - 2q_3a_y - 4q_2 + 8q_2q_1^2 + 8q_2^3 + 4q_2a_z\\
s_3 &= 4q_1^2q_3 - 2q_1a_x + 4q_2^2q_3 - 2q_2a_y
\end{aligned}
$$

A single normalized gradient-descent step is:

$$
q_{\nabla,t} = \hat q_{t-1} - \mu_t \frac{\nabla f}{\|\nabla f\|}
$$

---

## 4. Fusion

### Complementary blend

The gyro estimate and the gradient estimate are blended with a weight $\gamma$:

$$
q_{est} = \gamma\, q_\nabla + (1-\gamma)\, q_\omega
$$

### Collapsing the blend

Madgwick sets $\mu_t = \alpha\|\dot q_\omega\|\Delta t$ with $\alpha$ large, and chooses $\gamma$ so that the gyro drift rate equals the convergence rate. Under those choices, $\gamma \approx \beta\Delta t/\mu_t \to$ small. The blend then reduces to a single rate equation:

$$
\boxed{\;\dot{\hat q}_{est,t} = \dot q_{\omega,t} - \beta\,\frac{\nabla f}{\|\nabla f\|}\;}
$$

### Integration and normalization

$$
\boxed{\;\hat q_t = \hat q_{t-1} + \dot{\hat q}_{est,t}\,\Delta t,\qquad \hat q_t \leftarrow \frac{\hat q_t}{\|\hat q_t\|}\;}
$$

The gradient term acts like a feedback "error rate" that pulls the gyro integration toward the gravity-consistent orientation.

---

## 5. Choosing β

Let the gyro measurement error be $\tilde\omega_\beta$ (rad/s) per axis. The resulting quaternion-rate error magnitude is:

$$
\left\|\tfrac12 \hat q \otimes [0\;\tilde\omega_\beta\;\tilde\omega_\beta\;\tilde\omega_\beta]\right\| = \tfrac{\sqrt3}{2}\tilde\omega_\beta
$$

Therefore:

$$
\boxed{\beta = \sqrt{\tfrac34}\,\tilde\omega_\beta}
$$

Typical values are β ≈ 0.03–0.1. A higher β gives faster convergence but more accelerometer noise leaking into the estimate.

**Optional gyro bias compensation (MARG extension):**
- Compute the error rate $\omega_\epsilon = 2\hat q^* \otimes \frac{\nabla f}{\|\nabla f\|}$.
- Accumulate the bias estimate $\omega_b \mathrel{+}= \zeta\,\omega_\epsilon\Delta t$.
- Subtract $\omega_b$ from the raw gyro reading.
- Choose $\zeta = \sqrt{3/4}\,\dot{\tilde\omega}_\zeta$, where $\dot{\tilde\omega}_\zeta$ is the gyro bias drift rate.

---

## 6. Algorithm per Sample

1. Read $\omega$ (rad/s) and $a$. If $\|a\| = 0$, skip the correction step.
2. Normalize $a$.
3. Compute $\dot q_\omega = \tfrac12 q \otimes [0,\omega]$.
4. Compute $s = [s_0..s_3]$ and normalize it.
5. Compute $\dot q = \dot q_\omega - \beta s$.
6. Update $q \mathrel{+}= \dot q\,\Delta t$.
7. Normalize $q$.
8. Optionally output Euler angles:
   - Roll: $\phi = \operatorname{atan2}(2(q_0q_1+q_2q_3),\,1-2(q_1^2+q_2^2))$
   - Pitch: $\theta = \arcsin(2(q_0q_2-q_1q_3))$

---

## 7. Gimbal-Payload Considerations

- **Yaw is unobservable.** Gravity constrains only roll and pitch, so $q_3$-dominant heading drifts. Use motor encoders, a magnetometer (full MARG variant), or a host-vehicle heading to correct it.
- **Linear acceleration contamination.** Vehicle maneuvers bias the gravity estimate