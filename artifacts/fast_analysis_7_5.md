# AM32-Style 6-Step Commutation + BEMF Zero-Cross Detection

I don't know what "Cycle 7" refers to in your project, so I treated it as an iteration label. This is a clean-room implementation in the AM32 architecture style, not a copy of the AM32 source:

- Six-step trapezoidal commutation.
- Comparator-based BEMF sensing against a virtual neutral.
- Interrupt-driven zero-cross detection at speed, with polled detection at low speed and startup.
- Timer-scheduled commutation with timing advance.

It targets an STM32F0/G0-class MCU. Hardware access sits behind a thin HAL layer you map to your 4-in-1 board's pinout. For a 4-in-1 ESC, each MCU usually runs one motor, so this is a single-motor instance. The motor state lives in a struct, so you can instantiate it per MCU or per channel.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define ZC_FILTER_MIN           2     /* min consecutive comparator agreements   */
#define ZC_FILTER_MAX           12    /* max filter depth at low speed           */
#define POLLING_MODE_THRESHOLD  1500  /* commutation interval (us) above which we poll */
#define POLLING_MODE_HYST       200
#define BEMF_TIMEOUT_STEPS      10    /* missed ZCs before declaring desync      */
#define MIN_COMM_INTERVAL_US    20    /* ~ hard eRPM ceiling                     */
#define MAX_COMM_INTERVAL_US    20000 /* below this speed -> treat as stalled    */
#define DEFAULT_ADVANCE_LEVEL   2     /* 0..4 -> 0, 7.5, 15, 22.5, 30 deg        */

typedef enum {
    PHASE_FLOAT = 0,   /* both FETs off; BEMF measured here        */
    PHASE_LOW,         /* low-side FET held on                     */
    PHASE_PWM          /* high-side PWM, complementary low (sync)  */
} phase_mode_t;

typedef enum { PH_A = 0, PH_B, PH_C } phase_id_t;

typedef enum {
    ZC_EDGE_FALLING = 0,
    ZC_EDGE_RISING
} zc_edge_t;

typedef struct {
    /* commutation state */
    volatile uint8_t  step;              /* 1..6                               */
    volatile bool     forward;
    volatile bool     running;
    volatile bool     desync;

    /* zero-cross bookkeeping (timer ticks, 1 tick = 1 us) */
    volatile uint16_t last_zc_time;
    volatile uint16_t this_zc_time;
    volatile uint32_t comm_interval;     /* filtered time for one 60-deg step  */
    volatile uint16_t advance_ticks;
    volatile uint16_t wait_ticks;

    /* detection mode */
    volatile bool     polling_mode;
    volatile bool     zc_found;
    volatile uint8_t  filter_level;
    volatile uint8_t  bemf_timeout_count;
    volatile zc_edge_t expected_edge;

    /* config */
    uint8_t           advance_level;     /* 0..4                               */
    uint16_t          blanking_ticks;    /* post-commutation ignore window     */
} motor_t;

extern motor_t motor;

void comm_init(void);
void comm_start(void);
void comm_stop(void);
void commutate(void);

/* ISR entry points – call from your vector handlers */
void comm_comparator_isr(void);     /* COMP / EXTI line ISR                */
void comm_timer_isr(void);          /* commutation timer compare ISR       */
void comm_pwm_tick(void);           /* call each PWM period (polling mode) */

#endif
```

---

## `esc_hal.h` (board abstraction: map to your hardware)

```c
#ifndef ESC_HAL_H
#define ESC_HAL_H
#include <stdint.h>
#include <stdbool.h>
#include "commutation.h"

/* Gate drive */
void hal_phase_set(phase_id_t ph, phase_mode_t mode);

/* Comparator: select which floating phase feeds the + input
 * (virtual neutral is fixed on the - input).                     */
void hal_comp_select_phase(phase_id_t ph);
bool hal_comp_output(void);                  /* raw comparator level   */
void hal_comp_irq_enable(zc_edge_t edge);    /* arm EXTI on given edge */
void hal_comp_irq_disable(void);
void hal_comp_irq_clear(void);

/* Free-running 1 MHz interval timer (16-bit) */
uint16_t hal_interval_timer_now(void);
void     hal_interval_timer_reset(void);

/* One-shot commutation timer (1 MHz) */
void hal_comm_timer_schedule(uint16_t ticks); /* fire comm_timer_isr after N us */
void hal_comm_timer_cancel(void);

void hal_all_phases_float(void);
#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "esc_hal.h"

motor_t motor;

/* ------------------------------------------------------------------
 * Commutation table.
 * For each step: which phase is PWM (high), which is LOW, which FLOATs,
 * and the BEMF edge expected on the floating phase.
 *
 * Step  High  Low  Float  BEMF on float
 *  1     A     B     C     falling
 *  2     A     C     B     rising
 *  3     B     C     A     falling
 *  4     B     A     C     rising
 *  5     C     A     B     falling
 *  6     C     B     A     rising
 * ------------------------------------------------------------------ */
typedef struct {
    phase_id_t high;
    phase_id_t low;
    phase_id_t flt;
    zc_edge_t  edge;
} comm_step_t;

static const comm_step_t comm_table[7] = {
    [0] = { PH_A, PH_B, PH_C, ZC_EDGE_FALLING },   /* unused */
    [1] = { PH_A, PH_B, PH_C, ZC_EDGE_FALLING },
    [2] = { PH_A, PH_C, PH_B, ZC_EDGE_RISING  },
    [3] = { PH_B, PH_C, PH_A, ZC_EDGE_FALLING },
    [4] = { PH_B, PH_A, PH_C, ZC_EDGE_RISING  },
    [5] = { PH_C, PH_A, PH_B, ZC_EDGE_FALLING },
    [6] = { PH_C, PH_B, PH_A, ZC_EDGE_RISING  },
};

/* ------------------------------------------------------------------ */
static inline void apply_step(uint8_t step)
{
    const comm_step_t *s = &comm_table[step];

    /* Float first, then low, then PWM: avoids momentary shoot-through
     * paths when a phase transitions between roles. */
    hal_phase_set(s->flt,  PHASE_FLOAT);
    hal_phase_set(s->low,  PHASE_LOW);
    hal_phase_set(s->high, PHASE_PWM);

    hal_comp_select_phase(s->flt);

    /* Reversing direction walks the table backwards. That inverts the
     * slope seen on the floating phase. */
    zc_edge_t e = s->edge;
    if (!motor.forward)
        e = (e == ZC_EDGE_RISING) ? ZC_EDGE_FALLING : ZC_EDGE_RISING;
    motor.expected_edge = e;
}

/* Comparator output level that means "zero cross has happened".
 * Rising BEMF: phase goes above neutral -> comp output high.        */
static inline bool zc_target_level(void)
{
    return (motor.expected_edge == ZC_EDGE_RISING);
}

/* ------------------------------------------------------------------
 * Advance the step and drive the bridge.
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
    motor.bemf_timeout_count++;
    if (motor.bemf_timeout_count > BEMF_TIMEOUT_STEPS) {
        motor.desync = true;     /* main loop should restart/re-sync */
    }
}

/* ------------------------------------------------------------------
 * Arm detection for the new step.
 * Interrupt mode: wait out the blanking window, then enable the edge IRQ.
 * Polling mode:   comm_pwm_tick() handles detection.
 * ------------------------------------------------------------------ */
static void arm_zero_cross_detection(void)
{
    hal_comp_irq_disable();
    hal_comp_irq_clear();

    if (motor.polling_mode)
        return;

    /* Busy-wait the blanking period (short at high speed). It covers
     * demag/flyback ringing on the floating phase after commutation.
     * For long blanking, use a timer instead. */
    uint16_t t0 = hal_interval_timer_now();
    while ((uint16_t)(hal_interval_timer_now() - t0) < motor.blanking_ticks) { }

    /* If the comparator already reads the post-ZC level, the crossing
     * either happened during blanking or we're still in demag. Arm on
     * the edge anyway. The filter in the ISR rejects false triggers. */
    hal_comp_irq_enable(motor.expected_edge);
}

/* ------------------------------------------------------------------
 * Zero-cross confirmed: compute timing and schedule commutation.
 * ------------------------------