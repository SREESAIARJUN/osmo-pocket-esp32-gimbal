# AM32-Style Commutation State Machine and BEMF Zero-Crossing Detection

This is an original implementation in the style of AM32's 6-step sensorless architecture. It is not a verbatim copy of the GPL source. Each MCU on a 4-in-1 board runs one motor, so all state lives in a per-motor struct and the same code works on every channel.

## Hardware Abstraction Layer (`esc_hal.h`)

```c
#ifndef ESC_HAL_H
#define ESC_HAL_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

/* Phase drive modes */
void hal_phase_pwm(phase_t p);        /* complementary PWM on high side      */
void hal_phase_low(phase_t p);        /* low side fully on (sink)            */
void hal_phase_float(phase_t p);      /* both FETs off (BEMF sense)          */
void hal_all_phases_float(void);

/* Comparator: + input = floating phase, - input = virtual neutral */
void hal_comp_select_phase(phase_t p);
bool hal_comp_output(void);           /* raw comparator state                */
void hal_comp_irq_edge(bool rising);  /* EXTI edge selection                 */
void hal_comp_irq_enable(void);
void hal_comp_irq_disable(void);
void hal_comp_irq_clear(void);

/* Timers (1 tick = 0.5 us at 2 MHz is typical) */
uint16_t hal_interval_timer_read(void);   /* free-running, measures ZC-to-ZC */
void     hal_interval_timer_reset(void);
void     hal_com_timer_arm(uint16_t ticks); /* one-shot -> com_timer_isr()   */
void     hal_com_timer_disarm(void);

#endif
```

## Motor State and Configuration (`commutation.h`)

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H
#include "esc_hal.h"

#define ZC_FILTER_MIN          2      /* consecutive agreeing samples (high rpm) */
#define ZC_FILTER_MAX         12      /* (low rpm)                              */
#define POLL_MODE_THRESHOLD 4000      /* interval ticks above -> poll mode      */
#define INTERRUPT_MODE_THRESHOLD 2500 /* hysteresis back to IRQ mode            */
#define ZC_TIMEOUT_TICKS   20000      /* no ZC this long -> desync              */
#define MAX_INTERVAL_DELTA_PCT  50    /* sudden change considered bad ZC        */
#define BLANKING_MIN_TICKS     8
#define ADVANCE_LEVEL_DEFAULT  2      /* 0..4 => 0..30 electrical deg (7.5/step)*/

typedef enum {
    MOTOR_STOPPED,
    MOTOR_STARTING,     /* open-loop / polled detection */
    MOTOR_RUNNING,      /* closed-loop, interrupt ZC    */
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    volatile motor_state_t state;
    volatile uint8_t  step;               /* 1..6                               */
    volatile bool     reversed;
    volatile bool     rising;             /* expected BEMF edge this step       */
    volatile bool     poll_mode;          /* true = sample in main loop         */
    volatile bool     zc_found;

    volatile uint16_t thiszctime;
    volatile uint16_t lastzctime;
    volatile uint16_t commutation_interval;   /* filtered 60-deg period        */
    volatile uint16_t e_com_time;             /* full electrical period (6 steps)*/
    volatile uint16_t advance;                /* ticks                          */
    volatile uint16_t wait_time;

    uint8_t  advance_level;
    uint8_t  filter_level;
    uint8_t  bad_zc_count;
    uint32_t zc_timeout_count;
    uint32_t commutation_count;
} motor_t;

extern motor_t motor;

void motor_init(motor_t *m, bool reversed);
void commutate(motor_t *m);
void com_timer_isr(void);
void comp_isr(void);
void motor_poll_zero_cross(motor_t *m);   /* call from main loop / fast tick  */
void motor_desync_check(motor_t *m);

#endif
```

## Implementation (`commutation.c`)

```c
#include "commutation.h"

motor_t motor;

/* ------------------------------------------------------------------ *
 * Commutation table.
 * For each step: which phase gets PWM (high), which is low, which floats,
 * and the BEMF edge direction on the floating phase in FORWARD rotation.
 * ------------------------------------------------------------------ */
typedef struct {
    phase_t high;
    phase_t low;
    phase_t flt;
    bool    rising_fwd;
} com_step_t;

static const com_step_t com_table[7] = {
    /* 0 unused */ { PHASE_A, PHASE_A, PHASE_A, false },
    /* 1 */        { PHASE_A, PHASE_B, PHASE_C, false },  /* A+ B-  C float, falling */
    /* 2 */        { PHASE_C, PHASE_B, PHASE_A, true  },  /* C+ B-  A float, rising  */
    /* 3 */        { PHASE_C, PHASE_A, PHASE_B, false },  /* C+ A-  B float, falling */
    /* 4 */        { PHASE_B, PHASE_A, PHASE_C, true  },  /* B+ A-  C float, rising  */
    /* 5 */        { PHASE_B, PHASE_C, PHASE_A, false },  /* B+ C-  A float, falling */
    /* 6 */        { PHASE_A, PHASE_C, PHASE_B, true  },  /* A+ C-  B float, rising  */
};

/* ------------------------------------------------------------------ */
static inline void apply_phase_step(uint8_t step)
{
    const com_step_t *s = &com_table[step];
    /* Float first to avoid shoot-through between transitions */
    hal_phase_float(s->flt);
    hal_phase_low(s->low);
    hal_phase_pwm(s->high);
}

/* Configure comparator for the floating phase of the current step */
static inline void change_comp_input(motor_t *m)
{
    const com_step_t *s = &com_table[m->step];
    hal_comp_select_phase(s->flt);
    /* Reversing the direction walks the table backward, which flips the edge */
    m->rising = m->reversed ? !s->rising_fwd : s->rising_fwd;
    hal_comp_irq_edge(m->rising);
}

/* Adapt the digital filter to speed: more samples at low rpm (noisy),
 * fewer at high rpm (less time available). */
static inline void update_filter_level(motor_t *m)
{
    uint16_t ci = m->commutation_interval;
    if      (ci < 200)  m->filter_level = ZC_FILTER_MIN;
    else if (ci < 600)  m->filter_level = 3;
    else if (ci < 1500) m->filter_level = 5;
    else if (ci < 3000) m->filter_level = 8;
    else                m->filter_level = ZC_FILTER_MAX;
}

/* ------------------------------------------------------------------ *
 * Commutation: advance step, energise new phases, set up BEMF sensing.
 * ------------------------------------------------------------------ */
void commutate(motor_t *m)
{
    if (!m->reversed) {
        if (++m->step > 6) m->step = 1;
    } else {
        if (--m->step < 1) m->step = 6;
    }

    apply_phase_step(m->step);
    change_comp_input(m);

    m->commutation_count++;
    m->zc_found = false;
}

/* ------------------------------------------------------------------ *
 * Validate the comparator output with N consecutive agreeing reads.
 * After a ZC on a rising edge the comparator should read HIGH
 * (and LOW on falling).
 * ------------------------------------------------------------------ */
static bool zc_filter_pass(motor_t *m)
{
    bool expected = m->rising;
    for (uint8_t i = 0; i < m->filter_level; i++) {
        if (hal_comp_output() != expected) {
            return false;   /* noise / PWM ringing */
        }
    }
    return true;
}

/* ------------------------------------------------------------------ *
 * Common zero-cross handler (called from ISR or poll loop).
 * Measures the interval, computes the 30-deg delay minus advance,
 * and arms the commutation timer.
 * ------------------------------------------------------------------ */
static void handle_zero_cross(motor_t *m)
{
    m->thiszctime = hal_interval_timer_read();
    hal_interval_timer_reset();

    uint16_t new_interval = m->thiszctime;

    /* Sanity check: reject wildly inconsistent intervals while running */
    if (m->state == MOTOR_RUNNING && m->commutation_interval > 0) {
        uint32_t lo = (uint32_t)m->commutation_interval * (100 - MAX_INTERVAL_DELTA_PCT) / 100;
        uint32_t hi = (uint32_t)m->commutation_interval * (100 + MAX_INTERVAL_DELTA_PCT) / 100;
        if (new_interval < lo || new_interval > hi) {
            m->bad_zc_count++;
            new_interval = m->commutation_interval;   /* coast on old value */
        } else if (m->bad_zc_count) {
            m->bad_zc_count--;
        }
    }

    /* IIR smoothing: interval = (3*old + new) / 4 */
    m->commutation_interval =
        (uint16_t)(((uint32_t)m->commutation_interval * 3 + new_interval) >> 2);
    m->e_com_time = m->commutation_interval * 6;

    /* Advance: level * 7.5 deg; one