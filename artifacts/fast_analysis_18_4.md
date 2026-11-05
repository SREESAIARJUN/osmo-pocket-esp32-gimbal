# AM32-Style 6-Step Commutation & BEMF Zero-Cross Detection

I don't know what "Cycle 18" refers to in your project (a sprint, revision tag, or something else), so this is a self-contained implementation that follows AM32's architecture:

- Comparator-based BEMF sensing
- Polling at low speed, comparator-interrupt at high speed
- Interval timer plus commutation timer with timing advance

It targets an STM32F051-class part. The hardware abstraction is isolated in the `hal_*` functions so you can port it to G071, AT32, or GD32.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define COMM_STEPS                 6
#define ZC_FILTER_MIN              2      /* min consecutive comparator samples */
#define ZC_FILTER_MAX              12
#define POLLING_MODE_THRESHOLD     2000   /* commutation interval (us) above which we poll */
#define POLLING_MODE_HYSTERESIS    200
#define BEMF_TIMEOUT_US            20000  /* no ZC for this long => desync */
#define BEMF_TIMEOUT_MAX_COUNT     10
#define MIN_COMMUTATION_INTERVAL   40     /* us, hard e-rpm limit guard */
#define DEFAULT_TIMING_ADVANCE     2      /* units of 7.5 deg: 0..4 => 0..30 deg */

/* ---------- Phase identifiers ---------- */
typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

/* What each phase is doing during a step */
typedef enum {
    PH_FLOAT = 0,   /* both FETs off, BEMF sensed here */
    PH_PWM,         /* high-side PWM (complementary if enabled) */
    PH_LOW          /* low-side held on */
} phase_mode_t;

typedef struct {
    phase_mode_t a, b, c;
    phase_t      floating;      /* phase routed to comparator */
    bool         rising;        /* expected BEMF edge on floating phase */
} comm_step_t;

/* ---------- Runtime state ---------- */
typedef struct {
    volatile uint8_t  step;                 /* 1..6 */
    volatile bool     forward;
    volatile bool     running;
    volatile bool     polling_mode;
    volatile bool     zc_found;
    volatile bool     desync;

    volatile uint16_t commutation_interval; /* filtered, us */
    volatile uint16_t thiszctime;
    volatile uint16_t lastzctime;
    volatile uint16_t waitTime;
    volatile uint16_t advance;

    volatile uint8_t  filter_level;
    volatile uint8_t  bemf_timeout_count;
    volatile uint8_t  timing_advance;       /* 0..4 */

    volatile uint32_t zero_crosses;         /* since start */
    volatile uint32_t e_rpm;                /* x100 */
} esc_state_t;

extern esc_state_t esc;

void comm_init(void);
void comm_start(void);
void comm_stop(void);
void commutate(void);
void comm_poll_zero_cross(void);        /* call from main loop / 20kHz tick */
void comm_comparator_isr(void);         /* comparator EXTI handler */
void comm_timer_isr(void);              /* commutation timer compare handler */
void comm_tick_1khz(void);              /* desync / timeout supervision */

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "hal_esc.h"   /* see HAL contract at bottom */

esc_state_t esc;

/*
 * 6-step table, forward direction.
 * Step N: one phase PWM high, one low, one floating.
 * Floating phase BEMF edge alternates rising/falling.
 *
 *  step | A     B     C     | float | edge
 *   1   | PWM   LOW   FLT   |  C    | falling
 *   2   | PWM   FLT   LOW   |  B    | rising
 *   3   | FLT   PWM   LOW   |  A    | falling
 *   4   | LOW   PWM   FLT   |  C    | rising
 *   5   | LOW   FLT   PWM   |  B    | falling
 *   6   | FLT   LOW   PWM   |  A    | rising
 */
static const comm_step_t comm_table[COMM_STEPS] = {
    { PH_PWM,   PH_LOW,   PH_FLOAT, PHASE_C, false },
    { PH_PWM,   PH_FLOAT, PH_LOW,   PHASE_B, true  },
    { PH_FLOAT, PH_PWM,   PH_LOW,   PHASE_A, false },
    { PH_LOW,   PH_PWM,   PH_FLOAT, PHASE_C, true  },
    { PH_LOW,   PH_FLOAT, PH_PWM,   PHASE_B, false },
    { PH_FLOAT, PH_LOW,   PH_PWM,   PHASE_A, true  },
};

/* ------------------------------------------------------------------ */
/* Phase output helpers                                                */
/* ------------------------------------------------------------------ */
static inline void set_phase(phase_t p, phase_mode_t m)
{
    switch (m) {
    case PH_PWM:   hal_phase_pwm(p);   break;
    case PH_LOW:   hal_phase_low(p);   break;
    case PH_FLOAT:
    default:       hal_phase_float(p); break;
    }
}

/* Float first, then low, then PWM: avoids any instant of shoot-through
 * on the phase that's transitioning between high and low roles. */
static void apply_step(const comm_step_t *s)
{
    phase_mode_t modes[3] = { s->a, s->b, s->c };

    for (int p = 0; p < 3; p++) if (modes[p] == PH_FLOAT) set_phase((phase_t)p, PH_FLOAT);
    for (int p = 0; p < 3; p++) if (modes[p] == PH_LOW)   set_phase((phase_t)p, PH_LOW);
    for (int p = 0; p < 3; p++) if (modes[p] == PH_PWM)   set_phase((phase_t)p, PH_PWM);
}

/* Reverse direction: walk the table backwards and invert the expected edge. */
static inline uint8_t table_index(uint8_t step)
{
    return esc.forward ? (uint8_t)(step - 1) : (uint8_t)(COMM_STEPS - step);
}

static inline bool expected_rising(void)
{
    bool r = comm_table[table_index(esc.step)].rising;
    return esc.forward ? r : !r;
}

/* ------------------------------------------------------------------ */
/* Comparator routing                                                  */
/* ------------------------------------------------------------------ */
static void route_comparator(void)
{
    const comm_step_t *s = &comm_table[table_index(esc.step)];
    hal_comp_select_input(s->floating);  /* mux floating phase vs. virtual neutral */

    if (!esc.polling_mode) {
        /* Interrupt mode: arm only the edge we expect */
        hal_comp_exti_edge(expected_rising());
    }
}

/* ------------------------------------------------------------------ */
/* Commutate: advance state machine one step                           */
/* ------------------------------------------------------------------ */
void commutate(void)
{
    esc.step = (esc.step >= COMM_STEPS) ? 1 : (uint8_t)(esc.step + 1);

    hal_comp_exti_disable();                 /* mask during switching noise */
    apply_step(&comm_table[table_index(esc.step)]);
    route_comparator();

    esc.zc_found = false;

    /* Blanking: demagnetisation ringing after commutation.
     * Scale filter with speed: more samples at low speed, fewer at high. */
    if (esc.commutation_interval > 1000)      esc.filter_level = ZC_FILTER_MAX;
    else if (esc.commutation_interval > 400)  esc.filter_level = 6;
    else if (esc.commutation_interval > 150)  esc.filter_level = 4;
    else                                      esc.filter_level = ZC_FILTER_MIN;
}

/* ------------------------------------------------------------------ */
/* Zero-cross validation: N consecutive matching comparator reads      */
/* ------------------------------------------------------------------ */
static bool zc_filter_pass(void)
{
    const bool want = expected_rising();

    for (uint8_t i = 0; i < esc.filter_level; i++) {
        /* Comparator output high => phase above neutral.
         * Rising edge expected => after ZC, output should read high. */
        if (hal_comp_read() != want) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Zero-cross accepted: compute timing, schedule commutation           */
/* ------------------------------------------------------------------ */
static void on_zero_cross(void)
{
    esc.lastzctime = esc.thiszctime;
    esc.thiszctime = hal_interval_timer_read();
    hal_interval_timer_reset();

    uint16_t raw = esc.thiszctime;
    if (raw < MIN_COMMUTATION_INTERVAL) return;  /* reject impossible interval */

    /* IIR smoothing: 3/4 old + 1/4 new, plus half-weight on last two
     * ZCs to cancel the alternating rising/falling comparator offset. */
    uint16_t pair = (uint16_t)((raw + esc.lastzctime) >> 1);
    esc.commutation_interval =
        (uint16_t)(((uint32_t)esc.commutation_interval * 3 + pair) >> 2);

    /* Timing advance: 30 deg electrical = interval/2.
     * advance units of 7.5 deg => interval/8 per unit. */
    esc.advance  = (uint16_t)((esc.commutation_interval >> 3) * esc.timing_advance);
    uint16_t half = esc.commutation_interval >> 1;
    esc.waitTime = (half > esc.advance) ? (uint16_t)(half - esc.advance) : 1;

    esc.zc_found = true;
    esc.bemf_timeout_count = 0;
    esc.zero_crosses++;

    hal_comm_timer_schedule(esc.waitTime);   /* fires comm_timer_isr() */
}

/* ------------------------------------------------------------------ */
/* Low-speed path: polled from main loop or fast