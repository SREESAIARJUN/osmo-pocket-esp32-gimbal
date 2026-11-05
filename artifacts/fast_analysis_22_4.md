# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

This is an original implementation that follows AM32's architecture: comparator-interrupt zero-cross detection, timer-scheduled commutation with timing advance, and polling mode at low speed. Hardware access is abstracted, so you can bind it to your MCU (STM32F051/G071, AT32F421, GD32, etc.) and your 4-in-1 pinout.

I'm treating "Cycle 22" as your iteration label. If it refers to a specific spec revision, let me know what changed.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define COMM_TIMER_HZ            1000000u   /* 1 MHz commutation timer (1 us tick) */
#define ZC_FILTER_MIN            2u         /* min consecutive comparator agreements */
#define ZC_FILTER_MAX            12u        /* max filter at low speed               */
#define POLLING_MODE_ENTER_US    2000u      /* e_com > 2 ms  -> polling mode         */
#define POLLING_MODE_EXIT_US     1500u      /* hysteresis                            */
#define MIN_COMMUTATION_US       40u        /* ~ upper eRPM limit guard              */
#define MAX_COMMUTATION_US       20000u     /* below this speed = desync / stall     */
#define DESYNC_RATIO_NUM         2u         /* interval jump > 2x avg = suspicious   */
#define DESYNC_COUNT_LIMIT       6u
#define BLANKING_DIV             8u         /* blank 1/8 of interval after commutate */

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

typedef enum {
    PH_FLOAT = 0,   /* both FETs off                 */
    PH_LOW,         /* low-side FET on (static)      */
    PH_PWM          /* high-side PWM, complementary  */
} phase_mode_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,
    MOTOR_RUNNING,
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    volatile uint8_t  step;               /* 1..6 */
    volatile bool     forward;
    volatile bool     rising;             /* expected BEMF edge direction this step */
    volatile bool     polling_mode;
    volatile bool     zc_found;
    volatile motor_state_t state;

    volatile uint32_t last_zc_time;       /* timer ticks */
    volatile uint32_t this_zc_time;
    volatile uint32_t commutation_interval;      /* filtered, 60 deg electrical */
    volatile uint32_t commutation_intervals[6];  /* per-step history          */
    volatile uint32_t waitTime;                  /* ZC -> commutate delay      */
    volatile uint8_t  bemf_filter;               /* current filter level       */
    volatile uint8_t  timing_advance;            /* 0..4 -> 0, 7.5, 15, 22.5, 30 deg */

    volatile uint16_t good_zc_count;
    volatile uint8_t  desync_count;
    volatile uint32_t erpm;
} motor_t;

extern motor_t motor;

void comm_init(void);
void comm_start(bool forward);
void comm_stop(void);
void commutate(void);
void comm_zc_isr(void);            /* comparator interrupt  */
void comm_timer_isr(void);         /* commutation timer ISR */
void comm_poll(void);              /* call from fast loop (~20 kHz) when polling */

#endif
```

---

## `hal_esc.h` (bind to your hardware)

```c
#ifndef HAL_ESC_H
#define HAL_ESC_H
#include <stdint.h>
#include <stdbool.h>
#include "commutation.h"

/* Gate driver control */
void hal_phase_set(phase_t p, phase_mode_t m);

/* Comparator: select the floating phase as + input, virtual neutral as - input */
void hal_comp_select(phase_t floating_phase);
bool hal_comp_output(void);              /* true = phase > neutral */
void hal_comp_irq_enable(bool rising_edge);
void hal_comp_irq_disable(void);
void hal_comp_irq_clear(void);

/* Commutation timer (free-running 1 MHz counter + compare/overflow IRQ) */
uint32_t hal_timer_now(void);
void hal_timer_reset(void);
void hal_comm_timer_fire_in(uint32_t ticks);  /* one-shot ISR after ticks */
void hal_comm_timer_irq_disable(void);

/* Priority / critical sections */
void hal_irq_disable(void);
void hal_irq_enable(void);
#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "hal_esc.h"

motor_t motor;

/* ------------------------------------------------------------------
 * Commutation table (forward rotation).
 * Each step: which phase is PWM-high, which is LOW, which FLOATS,
 * and whether the floating phase BEMF will be RISING or FALLING.
 *
 * Step | PWM | LOW | FLOAT | BEMF on float
 *   1  |  A  |  B  |   C   | falling
 *   2  |  A  |  C  |   B   | rising
 *   3  |  B  |  C  |   A   | falling
 *   4  |  B  |  A  |   C   | rising
 *   5  |  C  |  A  |   B   | falling
 *   6  |  C  |  B  |   A   | rising
 * ------------------------------------------------------------------ */
typedef struct {
    phase_t pwm, low, flt;
    bool    rising;
} comm_step_t;

static const comm_step_t comm_table[7] = {
    { PHASE_A, PHASE_A, PHASE_A, false }, /* unused index 0 */
    { PHASE_A, PHASE_B, PHASE_C, false },
    { PHASE_A, PHASE_C, PHASE_B, true  },
    { PHASE_B, PHASE_C, PHASE_A, false },
    { PHASE_B, PHASE_A, PHASE_C, true  },
    { PHASE_C, PHASE_A, PHASE_B, false },
    { PHASE_C, PHASE_B, PHASE_A, true  },
};

/* ------------------------------------------------------------------ */
static inline uint32_t elapsed(uint32_t from, uint32_t to)
{
    return to - from;   /* wrap-safe for 32-bit free-running counter */
}

static void apply_step(uint8_t step)
{
    const comm_step_t *s = &comm_table[step];

    /* Order matters: float first to avoid shoot-through on any leg
       that changes from LOW to PWM or vice versa. */
    hal_phase_set(s->flt, PH_FLOAT);
    hal_phase_set(s->low, PH_LOW);
    hal_phase_set(s->pwm, PH_PWM);

    hal_comp_select(s->flt);

    /* In reverse, the step order is inverted, which flips the BEMF slope. */
    motor.rising = motor.forward ? s->rising : !s->rising;
}

/* Adaptive filter: more samples at low speed (more noise per interval),
   fewer at high speed (less time available). */
static void update_bemf_filter(void)
{
    uint32_t ci = motor.commutation_interval;
    if      (ci > 2000) motor.bemf_filter = ZC_FILTER_MAX;
    else if (ci > 800)  motor.bemf_filter = 8;
    else if (ci > 300)  motor.bemf_filter = 5;
    else if (ci > 120)  motor.bemf_filter = 3;
    else                motor.bemf_filter = ZC_FILTER_MIN;
}

static void update_polling_mode(void)
{
    if (!motor.polling_mode && motor.commutation_interval > POLLING_MODE_ENTER_US)
        motor.polling_mode = true;
    else if (motor.polling_mode && motor.commutation_interval < POLLING_MODE_EXIT_US)
        motor.polling_mode = false;
}

/* ------------------------------------------------------------------
 * commutate(): advance one step, re-arm detection.
 * ------------------------------------------------------------------ */
void commutate(void)
{
    if (motor.forward) {
        motor.step = (motor.step >= 6) ? 1 : motor.step + 1;
    } else {
        motor.step = (motor.step <= 1) ? 6 : motor.step - 1;
    }

    apply_step(motor.step);

    motor.zc_found = false;
    hal_comp_irq_clear();
    update_bemf_filter();
    update_polling_mode();

    if (motor.polling_mode) {
        hal_comp_irq_disable();           /* comm_poll() handles detection */
        return;
    }

    /* Blanking: ignore comparator during demagnetization / ringing.
       We arm the comparator after the blanking window via the timer ISR. */
    hal_comp_irq_disable();
    uint32_t blank = motor.commutation_interval / BLANKING_DIV;
    if (blank < 5) blank = 5;
    hal_comm_timer_fire_in(blank);         /* -> comm_timer_isr (blanking phase) */
}

/* ------------------------------------------------------------------
 * Comparator filter: require N consecutive agreeing reads.
 * Returns true if the zero crossing is confirmed.
 * ------------------------------------------------------------------ */
static bool zc_confirm(void)
{
    /* Post-crossing level: rising BEMF -> comp output HIGH */
    bool want = motor.rising;
    for (uint8_t i = 0; i < motor.bemf_filter; i++) {
        if (hal_comp_output() != want)
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------
 * Called once a ZC is confirmed (from ISR or poll).
 * Computes interval, timing advance, schedules commutation.
 * ------------------------------------------------------------------ */
static void handle_zero_cross(void)
{
    uint32_t now = hal_timer_now();
    uint32_t interval = elapsed(motor.last_zc_time, now);
    motor.last_zc_time = now;

    /* Desync heuristic: sudden interval collapse/explosion vs average */
    uint32_t avg