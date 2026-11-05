# AM32-Style 6-Step Commutation & BEMF Zero-Crossing Detection

This is a self-contained C implementation of the AM32 commutation architecture. The hardware layer is reduced to a small HAL interface so the core logic stays portable across F051, G071 and AT32 targets. The core logic is the commutation table, zero-cross filtering, interval averaging, timing advance, demag blanking, and desync recovery.

**Assumption about "Cycle 5":** I read it as revision 5 of the routine. This revision covers:

- hybrid polled and interrupt zero-cross detection
- an adaptive comparator filter
- demagnetization blanking
- bidirectional stepping
- desync recovery

I wrote this independently rather than copying AM32 source (which is GPL-3.0), so you can adapt it freely. If you plan to merge it into AM32 itself, follow that project's licensing.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Phase identifiers ---------- */
typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

/* ---------- Per-phase driver mode ---------- */
typedef enum {
    PH_FLOAT = 0,   /* both FETs off -> BEMF sensing        */
    PH_PWM,         /* high-side PWM (complementary if enabled) */
    PH_LOW          /* low-side held on                      */
} phase_mode_t;

/* ---------- One commutation step ---------- */
typedef struct {
    phase_t pwm_phase;
    phase_t low_phase;
    phase_t float_phase;
    bool    bemf_rising;   /* expected comparator edge on float phase */
} comm_step_t;

/* ---------- Motor runtime state ---------- */
typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,       /* open-loop / polled ZC, low eRPM      */
    MOTOR_RUNNING,        /* closed-loop, interrupt-driven ZC     */
    MOTOR_DESYNC          /* lost sync, recovering                */
} motor_state_t;

typedef struct {
    volatile motor_state_t state;
    volatile uint8_t  step;               /* 0..5                         */
    volatile bool     forward;

    /* timing (timer ticks, 1 tick = 0.5 us typical @ 2 MHz) */
    volatile uint32_t last_zc_time;
    volatile uint32_t this_zc_time;
    volatile uint32_t comm_interval;      /* filtered 60-deg interval     */
    volatile uint32_t avg_interval;       /* 4-step running average       */
    volatile uint32_t advance_ticks;
    volatile uint32_t wait_ticks;         /* ZC -> commutation delay       */

    /* zero-cross filtering */
    volatile uint8_t  filter_level;       /* required consecutive matches */
    volatile uint8_t  filter_count;
    volatile bool     zc_found;
    volatile bool     zc_irq_mode;        /* true = comparator interrupt  */
    volatile uint32_t blank_until;        /* demag blanking end tick       */

    /* health */
    volatile uint16_t bemf_timeout_cnt;
    volatile uint16_t good_zc_cnt;
    volatile uint32_t e_rpm;              /* electrical RPM / 100         */

    /* config */
    uint8_t  timing_advance_deg;          /* 0..30 electrical degrees     */
    uint16_t bemf_timeout_limit;
    bool     complementary_pwm;
} motor_t;

extern motor_t motor;

/* ---------- HAL hooks (implement per MCU) ---------- */
void     hal_phase_set(phase_t ph, phase_mode_t mode);
void     hal_comp_select(phase_t float_phase);      /* route float phase to COMP+ */
bool     hal_comp_output(void);                     /* raw comparator state       */
void     hal_comp_irq_enable(bool rising);
void     hal_comp_irq_disable(void);
uint32_t hal_timer_now(void);                       /* free-running interval timer */
void     hal_comm_timer_schedule(uint32_t ticks);   /* one-shot -> comm_timer_isr  */
void     hal_comm_timer_cancel(void);

/* ---------- API ---------- */
void commutation_init(void);
void commutation_start(bool forward);
void commutation_stop(void);
void commutate(void);
void zc_poll(void);              /* call from fast control loop (~20 kHz) */
void comparator_isr(void);       /* comparator / EXTI interrupt handler   */
void comm_timer_isr(void);       /* commutation timer compare handler     */

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"

motor_t motor;

/* ------------------------------------------------------------------
 * 6-step table (forward). Each step: PWM high, LOW sink, FLOAT sense.
 * BEMF on the float phase alternates rising/falling every step.
 * ------------------------------------------------------------------ */
static const comm_step_t step_table[6] = {
    /* pwm      low      float    rising */
    { PHASE_A, PHASE_B, PHASE_C, false },   /* 1: A+ B-  sense C falling */
    { PHASE_C, PHASE_B, PHASE_A, true  },   /* 2: C+ B-  sense A rising  */
    { PHASE_C, PHASE_A, PHASE_B, false },   /* 3: C+ A-  sense B falling */
    { PHASE_B, PHASE_A, PHASE_C, true  },   /* 4: B+ A-  sense C rising  */
    { PHASE_B, PHASE_C, PHASE_A, false },   /* 5: B+ C-  sense A falling */
    { PHASE_A, PHASE_C, PHASE_B, true  },   /* 6: A+ C-  sense B rising  */
};

/* ---------- Tunables ---------- */
#define TIMER_HZ                2000000UL   /* 0.5 us ticks              */
#define MIN_INTERVAL            40          /* ~20 us -> max eRPM guard   */
#define MAX_INTERVAL            20000       /* 10 ms  -> stall threshold  */
#define IRQ_MODE_ENTER_INTERVAL 1500        /* switch to IRQ above this speed */
#define IRQ_MODE_EXIT_INTERVAL  2500        /* hysteresis back to polling     */
#define FILTER_MAX              12
#define FILTER_MIN              2
#define DEMAG_BLANK_FRACTION    4           /* blank interval/4 after comm    */
#define GOOD_ZC_TO_RUN          30
#define INTERVAL_JUMP_LIMIT_NUM 2           /* reject ZC < interval/2 (noise) */

/* ------------------------------------------------------------------ */
static inline const comm_step_t *cur_step(void)
{
    return &step_table[motor.step];
}

/* Expected comparator level once the zero-cross has occurred.
 * In reverse, the step sequence runs backwards, so BEMF slope inverts. */
static inline bool expected_after_zc(void)
{
    bool rising = cur_step()->bemf_rising;
    if (!motor.forward) rising = !rising;
    return rising;      /* rising edge -> comparator reads high after ZC */
}

/* ------------------------------------------------------------------ */
static void apply_step(void)
{
    const comm_step_t *s = cur_step();

    /* Order matters: float first to avoid shoot-through and to start
       decay of the previous low/high phase immediately. */
    hal_phase_set(s->float_phase, PH_FLOAT);
    hal_phase_set(s->low_phase,   PH_LOW);
    hal_phase_set(s->pwm_phase,   PH_PWM);

    hal_comp_select(s->float_phase);
}

/* Adaptive filter: more samples at low speed (noisy, slow slopes),
 * fewer at high speed (little time between commutations). */
static void update_filter_level(void)
{
    uint32_t i = motor.avg_interval;
    uint8_t lvl;
    if      (i > 4000) lvl = FILTER_MAX;
    else if (i > 2000) lvl = 8;
    else if (i > 1000) lvl = 5;
    else if (i > 400)  lvl = 3;
    else               lvl = FILTER_MIN;
    motor.filter_level = lvl;
}

static void update_timing(uint32_t interval)
{
    /* Clamp absurd values (missed or false ZC) */
    if (interval < MIN_INTERVAL) interval = MIN_INTERVAL;
    if (interval > MAX_INTERVAL) interval = MAX_INTERVAL;

    /* IIR low-pass: comm = (3*comm + new) / 4 */
    motor.comm_interval = (motor.comm_interval * 3 + interval) >> 2;
    /* Slower average used for filter/mode decisions */
    motor.avg_interval  = (motor.avg_interval  * 7 + interval) >> 3;

    /* 60 electrical deg = comm_interval. Ideal ZC->comm delay = 30 deg.
       advance (deg) -> ticks = interval * adv / 60 */
    motor.advance_ticks = (motor.comm_interval * motor.timing_advance_deg) / 60;
    uint32_t half = motor.comm_interval >> 1;
    motor.wait_ticks = (half > motor.advance_ticks)
                       ? (half - motor.advance_ticks) : 1;

    /* eRPM/100 = 60 * TIMER_HZ / (6 * interval) / 100 */
    motor.e_rpm = (TIMER_HZ / 10) / (motor.comm_interval * 1);
}

/* ------------------------------------------------------------------
 * Commutate: advance one step, re-route comparator, set blanking.
 * ------------------------------------------------------------------ */
void commutate(void)
{
    if (motor.forward