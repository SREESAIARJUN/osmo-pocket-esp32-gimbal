# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection (C)

This is a self-contained, AM32-style implementation for a 4-in-1 ESC. Each of the four motor channels runs as an independent instance. It covers:

- The 6-step commutation state machine
- Comparator (BEMF) input steering for the floating phase
- Zero-crossing detection, using a polled filter at low speed and a comparator interrupt at high speed
- Timing advance, commutation interval averaging and desync/stall recovery

Two caveats:

- **The "Cycle 28" label.** I don't know what it refers to (a revision, a sprint or a variant in your codebase), so I've treated it as a revision tag in the header. Tell me if it implies specific behaviour.
- **The hardware layer.** It is abstracted behind a small HAL interface so you can bind it to STM32F0/G0/G4, AT32 or GD32 targets.

---

## `bemf_commutation.h`

```c
/* bemf_commutation.h  --  AM32-style sensorless 6-step core, rev "Cycle 28" */
#ifndef BEMF_COMMUTATION_H
#define BEMF_COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

#define ESC_MOTOR_COUNT        4

/* ---- Tunables ---------------------------------------------------------- */
#define ZC_FILTER_MAX          12u    /* max consecutive comparator agreements   */
#define ZC_FILTER_MIN          2u     /* min at high eRPM                         */
#define POLLING_MODE_THRESHOLD 2000u  /* commutation interval (us) above -> poll  */
#define INTERVAL_MAX_US        20000u /* longer = stalled / desynced              */
#define INTERVAL_MIN_US        12u    /* absolute eRPM ceiling guard              */
#define DESYNC_JUMP_PCT        50u    /* interval change % considered desync      */
#define DEFAULT_ADVANCE_DEG    15u    /* 0..30 electrical degrees (in 7.5 steps)  */
#define BLANKING_PCT           25u    /* % of interval to ignore after commutation*/

/* ---- Phase / step definitions ------------------------------------------ */
typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

typedef enum {
    PH_FLOAT = 0,   /* both FETs off, BEMF sensed here    */
    PH_HIGH,        /* high side PWM (complementary)       */
    PH_LOW          /* low side held on                    */
} phase_drive_t;

typedef enum {
    ZC_FALLING = 0, /* BEMF crosses from above to below neutral */
    ZC_RISING  = 1
} zc_edge_t;

typedef enum {
    MOTOR_STATE_IDLE = 0,
    MOTOR_STATE_ALIGN,
    MOTOR_STATE_OPEN_LOOP,    /* forced commutation ramp       */
    MOTOR_STATE_CLOSED_LOOP,  /* sensorless, ZC-driven         */
    MOTOR_STATE_BRAKE,
    MOTOR_STATE_FAULT
} motor_state_t;

/* ---- HAL binding (implement per MCU) ------------------------------------ */
typedef struct {
    void     (*set_phase)(uint8_t motor, phase_t ph, phase_drive_t mode);
    void     (*select_comparator)(uint8_t motor, phase_t floating_phase);
    bool     (*comparator_output)(uint8_t motor);           /* true = BEMF > neutral */
    void     (*comparator_irq_enable)(uint8_t motor, zc_edge_t edge);
    void     (*comparator_irq_disable)(uint8_t motor);
    uint32_t (*timer_now_us)(uint8_t motor);                /* free running 32-bit us */
    void     (*schedule_commutation)(uint8_t motor, uint32_t delay_us);
    void     (*cancel_commutation)(uint8_t motor);
    void     (*set_duty)(uint8_t motor, uint16_t duty);     /* 0..2000                */
} esc_hal_t;

/* ---- Per-motor runtime context ------------------------------------------ */
typedef struct {
    uint8_t        id;
    motor_state_t  state;
    uint8_t        step;              /* 1..6                               */
    bool           reversed;

    /* timing */
    uint32_t       last_zc_time;
    uint32_t       last_commutation_time;
    uint32_t       commutation_interval;   /* filtered, us per 60° electrical */
    uint32_t       zc_interval_raw;
    uint8_t        advance_deg;

    /* ZC detection */
    zc_edge_t      expected_edge;
    uint8_t        filter_level;
    uint8_t        filter_count;
    bool           zc_found;
    bool           polling_mode;
    bool           blanking;

    /* health */
    uint16_t       bad_zc_count;
    uint16_t       good_zc_count;
    uint16_t       desync_events;
    uint16_t       duty;
} motor_ctx_t;

/* ---- API ---------------------------------------------------------------- */
void esc_core_init(const esc_hal_t *hal);
void esc_motor_start(uint8_t m, bool reversed);
void esc_motor_stop(uint8_t m, bool brake);
void esc_set_throttle(uint8_t m, uint16_t duty);

/* ISR entry points -- wire these from your vector table */
void esc_comparator_isr(uint8_t m);       /* COMP / EXTI edge interrupt     */
void esc_commutation_timer_isr(uint8_t m);/* one-shot scheduled commutation */
void esc_fast_loop_poll(uint8_t m);       /* ~20-50 kHz, PWM-synchronous    */
void esc_slow_loop(uint8_t m);            /* ~1 kHz housekeeping            */

motor_ctx_t *esc_get_motor(uint8_t m);

#endif
```

---

## `bemf_commutation.c`

```c
/* bemf_commutation.c  --  AM32-style sensorless 6-step core, rev "Cycle 28" */
#include "bemf_commutation.h"
#include <string.h>

static const esc_hal_t *hal;
static motor_ctx_t motors[ESC_MOTOR_COUNT];

/* -------------------------------------------------------------------------
 * Commutation table (forward rotation).
 * Each step: [A, B, C] drive, the floating phase, and the BEMF edge expected
 * on that floating phase. Rising/falling alternates every step.
 *
 * Step | High | Low | Float | Edge
 *  1   |  A   |  B  |  C    | falling
 *  2   |  A   |  C  |  B    | rising
 *  3   |  B   |  C  |  A    | falling
 *  4   |  B   |  A  |  C    | rising
 *  5   |  C   |  A  |  B    | falling
 *  6   |  C   |  B  |  A    | rising
 * ------------------------------------------------------------------------- */
typedef struct {
    phase_drive_t drive[3];
    phase_t       floating;
    zc_edge_t     edge;
} comm_step_t;

static const comm_step_t comm_table[6] = {
    { { PH_HIGH,  PH_LOW,   PH_FLOAT }, PHASE_C, ZC_FALLING },
    { { PH_HIGH,  PH_FLOAT, PH_LOW   }, PHASE_B, ZC_RISING  },
    { { PH_FLOAT, PH_HIGH,  PH_LOW   }, PHASE_A, ZC_FALLING },
    { { PH_LOW,   PH_HIGH,  PH_FLOAT }, PHASE_C, ZC_RISING  },
    { { PH_LOW,   PH_FLOAT, PH_HIGH  }, PHASE_B, ZC_FALLING },
    { { PH_FLOAT, PH_LOW,   PH_HIGH  }, PHASE_A, ZC_RISING  },
};

/* ------------------------------------------------------------------------- */
static inline uint32_t elapsed(uint32_t now, uint32_t then)
{
    return now - then;               /* wrap-safe for 32-bit counters */
}

static inline uint8_t next_step(const motor_ctx_t *mc)
{
    if (!mc->reversed) return (mc->step >= 6) ? 1 : mc->step + 1;
    return (mc->step <= 1) ? 6 : mc->step - 1;
}

/* Edge polarity flips when running in reverse (BEMF slope inverts). */
static inline zc_edge_t step_edge(const motor_ctx_t *mc)
{
    zc_edge_t e = comm_table[mc->step - 1].edge;
    return mc->reversed ? (zc_edge_t)!e : e;
}

/* Filter scales with speed: more samples at low eRPM where noise dominates. */
static uint8_t compute_filter_level(uint32_t interval_us)
{
    if (interval_us > 1000) return ZC_FILTER_MAX;
    if (interval_us > 400)  return 8;
    if (interval_us > 150)  return 5;
    if (interval_us > 60)   return 3;
    return ZC_FILTER_MIN;
}

/* -------------------------------------------------------------------------
 * Apply a commutation step: drive the phases, steer the comparator to the
 * floating phase, and arm ZC detection with blanking.
 * ------------------------------------------------------------------------- */
static void apply_step(motor_ctx_t *mc)
{
    const comm_step_t *s = &comm_table[mc->step - 1];
    uint8_t m = mc->id;

    hal->comparator_irq_disable(m);

    /* Float first to avoid shoot-through during the transition. */
    for (int p = 0; p < 3; p++)
        if (s->drive[p] == PH_FLOAT) hal->set_phase(m, (phase_t)p, PH_FLOAT);
    for (int p = 0; p < 3; p++)
        if (s->drive[p] == PH_LOW)   hal->set_phase(m, (phase_t)p, PH_LOW);
    for (int p = 0; p < 3; p++)
        if (s->drive[p] == PH_HIGH)  hal->set_phase(m, (phase_t)p, PH_HIGH);

    hal->select_comparator(m, s->floating);

    mc->expected_edge = step_edge(mc);
    mc->filter_level  = compute_filter_level(mc->commutation_interval);
    mc->filter_count  = 0;
    mc->zc_found      = false;
    mc->blanking      