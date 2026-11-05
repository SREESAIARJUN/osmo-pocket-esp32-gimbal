# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

This is a self-contained C implementation of the sensorless commutation core used by AM32-style BLDC ESCs. It covers:

- the 6-step commutation state machine,
- comparator-based zero-cross detection (interrupt mode at speed, polled mode at low RPM),
- timing advance, blanking/demag rejection, and desync recovery.

Everything chip-specific sits behind a small HAL layer. You can map it to STM32G071, STM32F051, AT32F421, GD32E230 and similar parts. In a 4-in-1 ESC each MCU usually drives one motor, so the code is written per-motor. Multi-motor builds can instantiate the `motor_t` struct per channel.

> Note: AM32 itself is GPL-3.0. This is an independent implementation written in the same architectural style. If you merge it into AM32, the GPL applies.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Tunables ---------------- */
#define POLLING_MODE_THRESHOLD_US   4000u   /* above this interval -> polled ZC     */
#define INTERRUPT_MODE_THRESHOLD_US 2500u   /* below this interval -> ISR ZC (hyst) */
#define BEMF_FILTER_MIN             2u      /* comparator re-reads at high speed     */
#define BEMF_FILTER_MAX             8u      /* comparator re-reads at low speed      */
#define POLL_CONFIRM_COUNT          3u      /* consecutive polled hits for a ZC      */
#define MIN_COMMUTATION_INTERVAL_US 40u     /* ~ max eRPM guard                      */
#define MAX_COMMUTATION_INTERVAL_US 20000u  /* below this speed -> treat as stalled  */
#define DESYNC_RATIO_NUM            2u      /* new interval < old/2 -> suspicious    */
#define ZC_TIMEOUT_MULT             2u      /* no ZC in 2x interval -> desync        */
#define BEMF_TIMEOUT_LIMIT          10u     /* consecutive timeouts before restart   */

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;
typedef enum { ZC_FALLING = 0, ZC_RISING = 1 } zc_edge_t;
typedef enum { DIR_FORWARD = 0, DIR_REVERSE = 1 } motor_dir_t;

typedef struct {
    phase_t   pwm_phase;    /* high side PWM (complementary if comp PWM enabled) */
    phase_t   low_phase;    /* low side fully on                                 */
    phase_t   float_phase;  /* undriven - BEMF sensed here                       */
    zc_edge_t edge_fwd;     /* expected BEMF edge on float phase, forward dir    */
} comm_step_t;

typedef struct {
    /* state */
    volatile uint8_t  step;                 /* 1..6 */
    volatile bool     running;
    volatile bool     zc_isr_mode;          /* true = comparator interrupt mode    */
    volatile bool     zc_found;
    volatile bool     old_routine;          /* polled mode active                  */
    motor_dir_t       dir;

    /* timing (all in timer ticks, 1 tick = 1 us here) */
    volatile uint32_t commutation_interval; /* filtered time for one step (60 elec deg) */
    volatile uint32_t this_zc_time;
    volatile uint32_t last_zc_time;
    volatile uint32_t wait_time;            /* zc -> commutation delay              */
    volatile uint32_t blank_time;           /* post-commutation comparator blanking */
    uint8_t           timing_advance;       /* 0..4 => 0..30 deg in 7.5 deg units   */

    /* filtering / diagnostics */
    uint8_t           filter_level;
    volatile uint8_t  poll_hits;
    volatile uint16_t bemf_timeouts;
    volatile uint32_t zc_errors;
    uint8_t           duty_pct;             /* used by demag/blank heuristics       */
} motor_t;

extern motor_t motor;

void comm_init(void);
void comm_start(motor_dir_t dir);
void comm_stop(void);
void comm_commutate(void);          /* advance one step and drive bridge   */
void comm_zc_comparator_isr(void);  /* comparator EXTI/IRQ handler body     */
void comm_commutation_timer_isr(void); /* fires at scheduled commutation    */
void comm_poll_zc(void);            /* call from PWM-synchronous loop (~20kHz) */
void comm_10khz_tick(void);         /* housekeeping: mode switch, timeouts  */

#endif
```

---

## `hal_esc.h` (port these to your MCU)

```c
#ifndef HAL_ESC_H
#define HAL_ESC_H
#include <stdint.h>
#include <stdbool.h>
#include "commutation.h"

/* Bridge control */
void     hal_phase_pwm(phase_t p);     /* high side PWM, low side complementary */
void     hal_phase_low(phase_t p);     /* low side solidly on                   */
void     hal_phase_float(phase_t p);   /* both FETs off                         */
void     hal_all_off(void);

/* Comparator */
void     hal_comp_select(phase_t float_phase); /* mux: float phase vs virtual neutral */
bool     hal_comp_output(void);                /* raw comparator output level         */
void     hal_comp_irq_enable(zc_edge_t edge);  /* arm EXTI on given edge              */
void     hal_comp_irq_disable(void);

/* Timers: free-running 1 MHz interval timer + one-shot commutation timer */
uint32_t hal_interval_timer_read(void);
void     hal_interval_timer_reset(void);
void     hal_comm_timer_schedule(uint32_t ticks_from_now);
void     hal_comm_timer_cancel(void);

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "hal_esc.h"

motor_t motor;

/*
 * 6-step table (forward rotation).
 * The floating phase alternates rising/falling BEMF each step.
 * In reverse we walk the table backward and invert the expected edge.
 */
static const comm_step_t comm_table[7] = {
    /* idx 0 unused */ { PHASE_A, PHASE_A, PHASE_A, ZC_RISING },
    /* 1 */ { PHASE_A, PHASE_B, PHASE_C, ZC_FALLING },
    /* 2 */ { PHASE_A, PHASE_C, PHASE_B, ZC_RISING  },
    /* 3 */ { PHASE_B, PHASE_C, PHASE_A, ZC_FALLING },
    /* 4 */ { PHASE_B, PHASE_A, PHASE_C, ZC_RISING  },
    /* 5 */ { PHASE_C, PHASE_A, PHASE_B, ZC_FALLING },
    /* 6 */ { PHASE_C, PHASE_B, PHASE_A, ZC_RISING  },
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static inline zc_edge_t expected_edge(uint8_t step)
{
    zc_edge_t e = comm_table[step].edge_fwd;
    return (motor.dir == DIR_FORWARD) ? e : (zc_edge_t)!e;
}

/* Comparator level that indicates the zero-cross has HAPPENED.
 * Rising edge -> comparator now high; falling -> now low.
 * (Invert here if your comparator inputs are swapped on the PCB.) */
static inline bool zc_level(uint8_t step)
{
    return expected_edge(step) == ZC_RISING;
}

static void drive_step(uint8_t step)
{
    const comm_step_t *s = &comm_table[step];

    /* Float first to avoid shoot-through between phases changing role */
    hal_phase_float(s->float_phase);
    hal_phase_low(s->low_phase);
    hal_phase_pwm(s->pwm_phase);

    hal_comp_select(s->float_phase);
}

/* Filter strength scales with speed: more re-reads at low RPM where
 * PWM noise is proportionally larger, fewer at high RPM for latency. */
static void update_filter_level(void)
{
    uint32_t ci = motor.commutation_interval;
    if      (ci < 100)  motor.filter_level = BEMF_FILTER_MIN;
    else if (ci < 400)  motor.filter_level = 3;
    else if (ci < 1000) motor.filter_level = 5;
    else                motor.filter_level = BEMF_FILTER_MAX;
}

/* Blanking: ignore comparator right after commutation while the
 * de-energizing winding's freewheel current (demag) clamps the
 * floating phase to a rail. Longer at high duty / low speed. */
static void update_blank_time(void)
{