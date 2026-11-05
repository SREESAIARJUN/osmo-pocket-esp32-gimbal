# AM32-Style 6-Step Commutation and BEMF Zero-Cross Detection

This is a self-contained, portable C module modeled on the architecture AM32 uses on STM32F0/G0/AT32F4 targets. It is written fresh, not copied from AM32, which is GPL-3.0. If you merge it into AM32, keep it GPL-compatible.

"Cycle 15" was not defined in your request, so I treated it as a build or iteration label. If it refers to a specific requirement, such as a 15-cycle filter or a timer prescaler, it maps onto `ZC_FILTER_MAX` or `COMM_TIMER_HZ`.

## Design

The module follows AM32's model:

- **Commutation sequence:** a 6-step table. Each step defines a high-side phase, a low-side phase, a floating phase, and the BEMF polarity to expect.
- **Zero-cross detection:** a comparator compares the floating phase against the virtual neutral.
  - **Low RPM:** the comparator is polled from the control-loop interrupt (`zc_poll()`).
  - **High RPM:** the comparator EXTI interrupt is used (`zc_comparator_isr()`).
- **Glitch rejection:** blanking after each commutation, plus an N-sample consistency filter that scales with speed.
- **Timing:** the next commutation is scheduled at `interval/2 − advance` on a one-shot timer (`comm_timer_isr()`).
- **Fault handling:** desync and stall detection drop the motor back to open-loop startup.
- **Multi-motor support:** a per-motor context struct. On a typical 4-in-1, each motor has its own MCU and runs one instance. A single-MCU 4-in-1 runs four instances.

---

## `am32_comm.h`

```c
#ifndef AM32_COMM_H
#define AM32_COMM_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Configuration ---------------- */
#define COMM_TIMER_HZ          2000000u  /* 0.5 us tick on interval timer   */
#define ZC_FILTER_MIN          2u        /* consistent samples @ high RPM    */
#define ZC_FILTER_MAX          15u       /* consistent samples @ low RPM     */
#define ZC_BLANKING_MIN_TICKS  40u       /* min blanking after commutation   */
#define POLL_TO_IRQ_INTERVAL   4000u     /* < this (ticks) -> use comp IRQ   */
#define IRQ_TO_POLL_INTERVAL   5000u     /* hysteresis back to polling       */
#define MAX_INTERVAL_TICKS     40000u    /* slower than this = stalled       */
#define MIN_INTERVAL_TICKS     60u       /* faster = noise / desync          */
#define DESYNC_RATIO_NUM       2u        /* new interval < old/2 -> suspect  */
#define BEMF_TIMEOUT_LIMIT     10u       /* missed ZCs before restart        */
#define STARTUP_ZC_REQUIRED    20u       /* good ZCs before closed loop      */
#define STARTUP_STEP_TICKS     20000u    /* open-loop commutation period     */

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTUP,       /* open-loop forced commutation, hunting for ZC */
    MOTOR_RUNNING        /* closed-loop sensorless                       */
} motor_state_t;

typedef enum { ZC_MODE_POLLING = 0, ZC_MODE_INTERRUPT } zc_mode_t;

typedef struct {
    phase_t high;        /* PWM'd high side          */
    phase_t low;         /* low side held on         */
    phase_t floating;    /* BEMF sense phase         */
    bool    rising;      /* expected BEMF slope      */
} comm_step_t;

/* Hardware abstraction - implement per target (one set per motor id) */
typedef struct {
    void     (*phase_pwm)(phase_t p);     /* high side PWM, complementary low */
    void     (*phase_low)(phase_t p);     /* low side fully on                */
    void     (*phase_float)(phase_t p);   /* both FETs off                    */
    void     (*comp_select)(phase_t p);   /* mux floating phase to comp +    */
    bool     (*comp_read)(void);          /* comparator output level          */
    void     (*comp_irq_enable)(bool rising_edge);
    void     (*comp_irq_disable)(void);
    uint16_t (*interval_timer_now)(void); /* free-running 16-bit @ COMM_TIMER_HZ */
    void     (*interval_timer_reset)(void);
    void     (*comm_timer_fire_in)(uint16_t ticks); /* one-shot -> comm_timer_isr */
    void     (*comm_timer_stop)(void);
} motor_hal_t;

typedef struct {
    const motor_hal_t *hal;

    volatile motor_state_t state;
    volatile zc_mode_t     zc_mode;
    volatile uint8_t       step;              /* 1..6                       */
    bool                   reversed;

    /* timing */
    volatile uint16_t      thiszctime;
    volatile uint16_t      commutation_interval;   /* filtered, ticks / step */
    volatile uint16_t      last_interval;
    uint8_t                advance_level;          /* 0..32 -> 0..30 deg     */

    /* detection */
    volatile bool          zc_found;
    volatile bool          waiting_for_comm;
    volatile uint8_t       filter_level;
    volatile uint8_t       bemf_counter;           /* consistent samples     */
    volatile uint16_t      blank_until;            /* interval-timer tick    */
    volatile uint8_t       bemf_timeout;
    volatile uint16_t      zc_good_count;

    /* duty passthrough for the PWM layer */
    volatile uint16_t      duty;
} motor_t;

void motor_init(motor_t *m, const motor_hal_t *hal);
void motor_start(motor_t *m, bool reversed);
void motor_stop(motor_t *m);
void motor_set_advance(motor_t *m, uint8_t level);  /* 0..32 */

/* Call sites */
void zc_poll(motor_t *m);            /* from 20-40 kHz control loop / PWM ISR */
void zc_comparator_isr(motor_t *m);  /* from comparator EXTI                  */
void comm_timer_isr(motor_t *m);     /* from one-shot commutation timer       */
void motor_slow_tick(motor_t *m);    /* ~1 kHz: startup, stall supervision    */

#endif
```

---

## `am32_comm.c`

```c
#include "am32_comm.h"

/* -----------------------------------------------------------------------
 * 6-step table (forward). Each step: one phase PWM, one phase low,
 * one floating. Floating BEMF slope alternates falling/rising.
 *
 *  step | HIGH | LOW | FLOAT | BEMF
 *   1   |  A   |  B  |   C   | falling
 *   2   |  A   |  C  |   B   | rising
 *   3   |  B   |  C  |   A   | falling
 *   4   |  B   |  A  |   C   | rising
 *   5   |  C   |  A  |   B   | falling
 *   6   |  C   |  B  |   A   | rising
 * ---------------------------------------------------------------------- */
static const comm_step_t comm_table[7] = {
    { PHASE_A, PHASE_B, PHASE_C, false },  /* [0] unused                    */
    { PHASE_A, PHASE_B, PHASE_C, false },
    { PHASE_A, PHASE_C, PHASE_B, true  },
    { PHASE_B, PHASE_C, PHASE_A, false },
    { PHASE_B, PHASE_A, PHASE_C, true  },
    { PHASE_C, PHASE_A, PHASE_B, false },
    { PHASE_C, PHASE_B, PHASE_A, true  },
};

/* ---------------- helpers ---------------- */

static inline uint16_t elapsed(motor_t *m, uint16_t since)
{
    return (uint16_t)(m->hal->interval_timer_now() - since); /* wrap-safe */
}

/* The comparator output that means "BEMF has crossed neutral" for this step.
 * Falling BEMF: comp goes high->low, so crossed when output == 0. */
static inline bool zc_target_level(const comm_step_t *s)
{
    return s->rising;
}

/* Scale filter with speed: long intervals get more samples (noisier,
 * more PWM edges per step); short intervals get fewer so we don't miss ZC. */
static void update_filter_level(motor_t *m)
{
    uint16_t ci = m->commutation_interval;
    uint8_t f;
    if      (ci > 12000u) f = ZC_FILTER_MAX;
    else if (ci >  6000u) f = 10u;
    else if (ci >  3000u) f = 6u;
    else if (ci >  1500u) f = 4u;
    else if (ci >   600u) f = 3u;
    else                  f = ZC_FILTER_MIN;
    if (m->state == MOTOR_STARTUP) f = ZC_FILTER_MAX;
    m->filter_level = f;
}

/* ---------------- phase driving ---------------- */

static void apply_step(motor_t *m)
{
    const comm_step_t *s = &comm_table[m->step];
    const motor_hal_t *h = m->hal;

    /* Order matters: float first to avoid