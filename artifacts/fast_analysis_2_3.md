# AM32-Style 6-Step Commutation & BEMF Zero-Crossing Detection (Cycle 2)

This is a clean-room C implementation modeled on AM32's architecture: sensorless 6-step trapezoidal commutation with comparator-based BEMF zero-crossing detection. It is hardware-abstracted, so you port it by filling in the `hal_*` functions for your MCU (F051, G071, AT32F421, and so on).

Cycle 2 changes compared with a basic version:
- RPM-adaptive comparator filtering
- Interrupt mode at speed, polling mode at low speed and startup
- Weighted interval averaging with outlier rejection
- Timing advance applied as a fraction of 30°
- Desync detection and recovery
- Per-motor instance structs, so one codebase serves 4-in-1 boards (one MCU per motor, or multi-instance)

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Tunables ---------------- */
#define COMM_TIMER_HZ            1000000UL   /* 1 MHz -> 1 us ticks          */
#define MIN_FILTER_LEVEL         2           /* comparator samples @ high rpm */
#define MAX_FILTER_LEVEL         12          /* comparator samples @ low rpm  */
#define POLLING_MODE_THRESHOLD   2000        /* us/step: above -> poll        */
#define BLANKING_PCT             25          /* % of interval ignored post-comm */
#define MIN_BLANKING_US          20
#define ZC_TIMEOUT_MULT          4           /* no ZC in 4x interval -> desync */
#define STARTUP_GOOD_ZC_REQUIRED 30
#define MAX_INTERVAL_US          20000       /* slower than this = stalled    */
#define MIN_INTERVAL_US          30          /* ~ eRPM limit guard            */
#define DESYNC_JUMP_PCT          50          /* interval change > 50% = bad   */

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

typedef enum {
    PHASE_FLOAT = 0,
    PHASE_PWM_HIGH,      /* complementary PWM on high side */
    PHASE_LOW_ON         /* low side held on               */
} phase_drive_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTUP,       /* open-loop / polling, gathering ZCs */
    MOTOR_RUNNING,       /* closed-loop, interrupt-driven      */
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    phase_t high;
    phase_t low;
    phase_t floating;
    bool    bemf_rising; /* expected comparator edge on floating phase */
} comm_step_t;

typedef struct {
    uint8_t  id;                    /* 0..3 on 4-in-1                     */
    volatile motor_state_t state;
    volatile uint8_t step;          /* 1..6                               */
    bool     reversed;

    /* timing (all in timer ticks = us) */
    volatile uint32_t last_zc_time;
    volatile uint32_t last_comm_time;
    volatile uint32_t zc_interval;  /* raw ZC-to-ZC                       */
    volatile uint32_t comm_interval;/* filtered, per 60 deg step          */
    uint8_t  advance_level;         /* 0..4, each = 7.5 deg               */

    /* detection */
    uint8_t  filter_level;
    volatile bool    zc_found;
    volatile bool    polling_mode;
    volatile bool    comm_pending;
    uint16_t good_zc_count;
    uint16_t bad_zc_count;
    uint32_t desync_count;
} motor_t;

/* ---- API ---- */
void motor_init(motor_t *m, uint8_t id);
void motor_start(motor_t *m);
void motor_stop(motor_t *m);
void motor_commutate(motor_t *m);
void motor_poll_zero_cross(motor_t *m);       /* call from fast loop        */
void motor_comparator_isr(motor_t *m);        /* comparator EXTI handler    */
void motor_comm_timer_isr(motor_t *m);        /* scheduled-commutation ISR  */
void motor_watchdog_tick(motor_t *m);         /* call ~every 1 ms           */
uint32_t motor_get_erpm(const motor_t *m);

/* ---- HAL (implement per MCU) ---- */
extern uint32_t hal_timer_now(uint8_t id);
extern void     hal_comm_timer_schedule(uint8_t id, uint32_t delay_ticks);
extern void     hal_comm_timer_cancel(uint8_t id);
extern void     hal_set_phase(uint8_t id, phase_t p, phase_drive_t d);
extern void     hal_comp_select_input(uint8_t id, phase_t floating);
extern bool     hal_comp_read(uint8_t id);       /* true = BEMF > neutral */
extern void     hal_comp_irq_enable(uint8_t id, bool rising_edge);
extern void     hal_comp_irq_disable(uint8_t id);
extern void     hal_all_phases_off(uint8_t id);

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"

/* 6-step table (forward). The floating phase alternates BEMF polarity.
 * Step: High  Low  Float  Edge
 *  1     A     B     C    falling
 *  2     A     C     B    rising
 *  3     B     C     A    falling
 *  4     B     A     C    rising
 *  5     C     A     B    falling
 *  6     C     B     A    rising
 */
static const comm_step_t comm_table[7] = {
    { 0 },                                        /* unused index 0 */
    { PHASE_A, PHASE_B, PHASE_C, false },
    { PHASE_A, PHASE_C, PHASE_B, true  },
    { PHASE_B, PHASE_C, PHASE_A, false },
    { PHASE_B, PHASE_A, PHASE_C, true  },
    { PHASE_C, PHASE_A, PHASE_B, false },
    { PHASE_C, PHASE_B, PHASE_A, true  },
};

/* ---------------------------------------------------------------- */
/*  Helpers                                                          */
/* ---------------------------------------------------------------- */

static inline uint32_t elapsed(uint32_t now, uint32_t then)
{
    return now - then;                 /* wrap-safe with 32-bit free-running timer */
}

static void apply_step(motor_t *m)
{
    const comm_step_t *s = &comm_table[m->step];

    /* Order matters: float first to avoid shoot-through on transition */
    hal_set_phase(m->id, s->floating, PHASE_FLOAT);
    hal_set_phase(m->id, s->low,      PHASE_LOW_ON);
    hal_set_phase(m->id, s->high,     PHASE_PWM_HIGH);

    hal_comp_select_input(m->id, s->floating);
}

/* Expected edge, accounting for direction. Reversing walks the table
 * backwards, which inverts the BEMF slope on the floating phase. */
static inline bool expected_rising(const motor_t *m)
{
    bool r = comm_table[m->step].bemf_rising;
    return m->reversed ? !r : r;
}

/* Filter strength scales with interval: long interval = noisy low-BEMF,
 * so more samples. Short interval = need fast response. */
static void update_filter_level(motor_t *m)
{
    uint32_t ci = m->comm_interval;
    if      (ci > 1500) m->filter_level = MAX_FILTER_LEVEL;
    else if (ci > 800)  m->filter_level = 8;
    else if (ci > 400)  m->filter_level = 5;
    else if (ci > 150)  m->filter_level = 3;
    else                m->filter_level = MIN_FILTER_LEVEL;

    m->polling_mode = (m->state == MOTOR_STARTUP) ||
                      (ci > POLLING_MODE_THRESHOLD);
}

static inline uint32_t blanking_ticks(const motor_t *m)
{
    uint32_t b = (m->comm_interval * BLANKING_PCT) / 100;
    return (b < MIN_BLANKING_US) ? MIN_BLANKING_US : b;
}

/* ---------------------------------------------------------------- */
/*  Init / start / stop                                              */
/* ---------------------------------------------------------------- */

void motor_init(motor_t *m, uint8_t id)
{
    *m = (motor_t){0};
    m->id            = id;
    m->step          = 1;
    m->state         = MOTOR_STOPPED;
    m->comm_interval = MAX_INTERVAL_US;
    m->advance_level = 2;               /* 15 deg default */
    m->filter_level  = MAX_FILTER_LEVEL;
    hal_all_phases_off(id);
}

void motor_start(motor_t *m)
{
    m->state          = MOTOR_STARTUP;
    m->good_zc_count  = 0;
    m->bad_zc_count   = 0;
    m->comm_interval  = 10000;          /* initial guess, 10 ms/step */
    m->zc_found       = false;
    m->comm_pending   = false;
    m->polling_mode   = true;
    m->last_comm_time = hal_timer_now(m->id);
    m->last_zc_time   = m->last_comm_time;
    motor_commutate(m);
}

void motor_stop(motor_t *m)
{
    m->state =