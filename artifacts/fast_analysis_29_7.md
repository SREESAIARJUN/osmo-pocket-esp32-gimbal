# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

This is an original, clean-room implementation that follows the architecture of AM32-class sensorless ESC firmware. It is not a copy of the AM32 source. On a 4-in-1 board, each motor channel usually has its own MCU, so each MCU runs one instance of this code.

It targets an STM32G0/F0-class MCU with:
- Complementary PWM on TIM1
- An internal comparator with a muxed phase input against a virtual neutral
- A free-running 1 MHz interval timer
- A one-shot commutation timer

All hardware access goes through a thin HAL layer (`hal_*`), so you can port it to your MCU.

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Tunables ---------------- */
#define TIMER_TICKS_PER_US         1u       /* interval timer = 1 MHz            */
#define ZC_POLL_MODE_THRESHOLD     2000u    /* interval (ticks) above which poll */
#define ZC_IRQ_MODE_THRESHOLD      1600u    /* hysteresis to return to IRQ mode  */
#define ZC_FILTER_MAX              12u      /* consecutive matching samples      */
#define ZC_FILTER_MIN              2u
#define BLANKING_MIN_TICKS         8u       /* min blanking after commutation    */
#define ZC_TIMEOUT_TICKS           20000u   /* no ZC within this -> desync       */
#define MAX_DESYNC_COUNT           10u
#define DEFAULT_TIMING_ADVANCE     2u       /* units of 7.5 deg (2 = 15 deg)     */

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;
typedef enum { DRIVE_FLOAT = 0, DRIVE_LOW, DRIVE_PWM } drive_t;
typedef enum { ZC_RISING = 0, ZC_FALLING } zc_edge_t;

typedef struct {
    volatile uint8_t  step;                 /* 0..5                              */
    volatile bool     forward;
    volatile bool     running;
    volatile bool     zc_found;             /* ZC seen in current step           */
    volatile bool     polling_mode;         /* low-speed: poll comparator        */
    volatile uint32_t commutation_interval; /* filtered 60 deg period (ticks)    */
    volatile uint32_t last_zc_time;
    volatile uint32_t this_zc_time;
    volatile uint32_t last_commutation_time;
    volatile uint8_t  filter_level;
    volatile uint8_t  filter_count;
    volatile uint8_t  timing_advance;       /* 0..4 (x 7.5 deg)                  */
    volatile uint16_t desync_count;
    volatile uint32_t good_zc_count;
} motor_state_t;

extern motor_state_t motor;

void commutation_init(void);
void commutate(void);
void zc_poll(void);              /* call from fast main loop / 20 kHz tick      */
void comparator_isr(void);       /* comparator edge IRQ                         */
void commutation_timer_isr(void);/* one-shot commutation timer IRQ              */
void zc_timeout_check(void);     /* call periodically                           */

#endif
```

## `commutation.c`

```c
#include "commutation.h"
#include "hal.h"   /* hal_set_phase(), hal_comp_*, hal_timer_*, hal_irq_* */

motor_state_t motor;

/* --------------------------------------------------------------------------
 * 6-step commutation table (forward direction).
 * Each step: one phase PWM (high side), one phase LOW, one phase FLOAT.
 * The floating phase is fed to the comparator; its BEMF crosses neutral
 * with alternating polarity.
 *
 *   step | A    B    C    | sense | edge (fwd)
 *   0    | PWM  LOW  FLT  |  C    | falling
 *   1    | PWM  FLT  LOW  |  B    | rising
 *   2    | FLT  PWM  LOW  |  A    | falling
 *   3    | LOW  PWM  FLT  |  C    | rising
 *   4    | LOW  FLT  PWM  |  B    | falling
 *   5    | FLT  LOW  PWM  |  A    | rising
 *
 * Reverse direction walks the table backwards. In that case every
 * floating-phase edge polarity is inverted.
 * -------------------------------------------------------------------------- */
typedef struct {
    drive_t   drive[3];
    phase_t   sense;
    zc_edge_t edge_fwd;
} comm_step_t;

static const comm_step_t comm_table[6] = {
    { { DRIVE_PWM,   DRIVE_LOW,   DRIVE_FLOAT }, PHASE_C, ZC_FALLING },
    { { DRIVE_PWM,   DRIVE_FLOAT, DRIVE_LOW   }, PHASE_B, ZC_RISING  },
    { { DRIVE_FLOAT, DRIVE_PWM,   DRIVE_LOW   }, PHASE_A, ZC_FALLING },
    { { DRIVE_LOW,   DRIVE_PWM,   DRIVE_FLOAT }, PHASE_C, ZC_RISING  },
    { { DRIVE_LOW,   DRIVE_FLOAT, DRIVE_PWM   }, PHASE_B, ZC_FALLING },
    { { DRIVE_FLOAT, DRIVE_LOW,   DRIVE_PWM   }, PHASE_A, ZC_RISING  },
};

static inline zc_edge_t expected_edge(uint8_t step)
{
    zc_edge_t e = comm_table[step].edge_fwd;
    return motor.forward ? e : (e == ZC_RISING ? ZC_FALLING : ZC_RISING);
}

/* Comparator output: 1 = phase above virtual neutral.
 * Rising ZC is complete when output reads 1; falling when it reads 0. */
static inline bool zc_condition_met(uint8_t step)
{
    bool out = hal_comp_read();
    return (expected_edge(step) == ZC_RISING) ? out : !out;
}

/* -------------------------------------------------------------------------- */
static void apply_step(uint8_t step)
{
    const comm_step_t *s = &comm_table[step];

    /* Float phases first so no leg ever conducts against another (shoot-through
     * safety). Then drive LOW, then enable PWM. */
    for (int p = 0; p < 3; p++)
        if (s->drive[p] == DRIVE_FLOAT) hal_set_phase((phase_t)p, DRIVE_FLOAT);
    for (int p = 0; p < 3; p++)
        if (s->drive[p] == DRIVE_LOW)   hal_set_phase((phase_t)p, DRIVE_LOW);
    for (int p = 0; p < 3; p++)
        if (s->drive[p] == DRIVE_PWM)   hal_set_phase((phase_t)p, DRIVE_PWM);

    hal_comp_select_input(s->sense);
    hal_comp_set_irq_edge(expected_edge(step) == ZC_RISING);
}

/* Filter depth scales with speed. Low speed (long interval) gets heavy
 * filtering. High speed gets less filtering, since there are fewer samples
 * per step. */
static void update_filter_level(void)
{
    uint32_t ci = motor.commutation_interval;
    uint8_t lvl;
    if      (ci > 4000) lvl = ZC_FILTER_MAX;
    else if (ci > 2000) lvl = 8;
    else if (ci > 1000) lvl = 5;
    else if (ci > 400)  lvl = 3;
    else                lvl = ZC_FILTER_MIN;
    motor.filter_level = lvl;
}

/* --------------