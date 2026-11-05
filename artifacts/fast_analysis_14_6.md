# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection (Cycle 14)

This is an original implementation written in the style of AM32's architecture. It is not copied from the GPL source. It targets one ESC channel of a 4-in-1 board. Each MCU on a 4-in-1 runs its own instance, as with AM32.

The hardware layer is abstracted behind small inline hooks, so you can map it to STM32F051, G071, AT32F421, and similar parts.

## Header: `commutation.h`

```c
/* commutation.h  --  Cycle 14
 * 6-step trapezoidal commutation + comparator BEMF ZC detection
 */
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define COMM_TIMER_HZ            1000000u   /* 1 MHz commutation/interval timer        */
#define POLLING_MODE_ENTER_INTV  2500u      /* us/step: below this speed -> polling     */
#define POLLING_MODE_EXIT_INTV   2000u      /* hysteresis                               */
#define MAX_COMM_INTERVAL        20000u     /* us/step: slower than this = stalled      */
#define MIN_COMM_INTERVAL        25u        /* us/step: hard eRPM ceiling (~400k eRPM)  */
#define BEMF_FILTER_MIN          2u
#define BEMF_FILTER_MAX          12u
#define DESYNC_BAD_COUNT_LIMIT   8u
#define DEFAULT_ADVANCE_LEVEL    2u         /* 0..4  -> 0, 7.5, 15, 22.5, 30 deg (elec) */

typedef enum {
    PHASE_A = 0,
    PHASE_B = 1,
    PHASE_C = 2
} phase_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,       /* open-loop / polling zero-cross                   */
    MOTOR_RUNNING         /* closed-loop, comparator interrupt driven          */
} motor_state_t;

typedef struct {
    volatile uint8_t       step;              /* 1..6                              */
    volatile bool          forward;
    volatile bool          rising;            /* expected BEMF edge this step     */
    volatile bool          zc_found;
    volatile bool          polling_mode;
    volatile bool          old_routine;       /* true = poll comparator in loop   */
    volatile motor_state_t state;

    volatile uint32_t      commutation_interval;  /* filtered, timer ticks/step    */
    volatile uint32_t      this_zc_time;
    volatile uint32_t      last_zc_time;
    volatile uint32_t      waitTime;              /* ZC -> commutation delay       */
    volatile uint32_t      zc_timeout;

    volatile uint8_t       filter_level;
    volatile uint8_t       bad_count;
    volatile uint8_t       advance_level;         /* 0..4                          */
    volatile uint16_t      zero_crosses;          /* since start, saturating       */

    volatile uint32_t      e_com_time;            /* 6-step electrical period      */
} comm_ctx_t;

extern comm_ctx_t g_comm;

void comm_init(bool forward);
void comm_start(void);
void comm_stop(void);
void comm_commutate(void);
void comm_set_step(uint8_t step);
void comm_poll_zero_cross(void);       /* call fast from main loop when polling  */
void comm_comparator_isr(void);        /* comparator EXTI IRQ                    */
void comm_timer_isr(void);             /* commutation timer compare IRQ          */
void comm_10khz_tick(void);            /* timeout / desync supervision           */

#endif
```

## Hardware Abstraction: `hal_esc.h`

Adapt the pins and registers to your target. Phase outputs map to the TIM1 complementary channels and the low-side GPIOs, as on a typical F051 or G071 AM32 target.

```c
#ifndef HAL_ESC_H
#define HAL_ESC_H
#include <stdint.h>
#include <stdbool.h>
#include "commutation.h"

/* --- Phase driver primitives ----------------------------------------- */
void hal_phase_pwm(phase_t p);     /* high side PWM, low side complementary (if comp PWM) */
void hal_phase_low(phase_t p);     /* low side fully on, high side off                   */
void hal_phase_float(phase_t p);   /* both FETs off (BEMF sense phase)                   */

/* --- Comparator ------------------------------------------------------ */
void  hal_comp_select_input(phase_t p);   /* mux floating phase vs virtual neutral   */
bool  hal_comp_output(void);              /* raw comparator output                   */
void  hal_comp_irq_enable(bool rising_edge);
void  hal_comp_irq_disable(void);
void  hal_comp_irq_clear(void);

/* --- Timers ---------------------------------------------------------- */
uint32_t hal_interval_timer_read(void);   /* free running @ COMM_TIMER_HZ            */
void     hal_interval_timer_reset(void);
void     hal_comm_timer_schedule(uint32_t ticks_from_now);
void     hal_comm_timer_cancel(void);

/* --- Misc ------------------------------------------------------------ */
static inline void hal_irq_disable(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void hal_irq_enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }
#endif
```

## Implementation: `commutation.c`

```c
/* commutation.c  --  Cycle 14 */
#include "commutation.h"
#include "hal_esc.h"

comm_ctx_t g_comm;

/* ======================================================================
 * Commutation table
 * ----------------------------------------------------------------------
 * step | PWM(high) | LOW | FLOAT | float BEMF edge (forward)
 *  1   |    A      |  B  |   C   | falling
 *  2   |    C      |  B  |   A   | rising
 *  3   |    C      |  A  |   B   | falling
 *  4   |    B      |  A  |   C   | rising
 *  5   |    B      |  C  |   A   | falling
 *  6   |    A      |  C  |   B   | rising
 * Reverse direction walks the table 6->1 and inverts edge polarity.
 * ====================================================================== */
typedef struct {
    phase_t pwm;
    phase_t low;
    phase_t flt;
    bool    rising_fwd;
} step_def_t;

static const step_def_t k_steps[7] = {
    /* 0 unused */ { PHASE_A, PHASE_A, PHASE_A, false },
    /* 1 */        { PHASE_A, PHASE_B, PHASE_C, false },
    /* 2 */        { PHASE_C, PHASE_B, PHASE_A, true  },
    /* 3 */        { PHASE_C, PHASE_A, PHASE_B, false },
    /* 4 */        { PHASE_B, PHASE_A, PHASE_C, true  },
    /* 5 */        { PHASE_B, PHASE_C, PHASE_A, false },
    /* 6 */        { PHASE_A, PHASE_C, PHASE_B, true  },
};

/* ---------------------------------------------------------------------- */
static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Filter depth scales with step length: long steps get more samples
 * (PWM ringing dominates), short steps fewer (need fast response). */
static uint8_t compute_filter_level(uint32_t interval)
{
    if (interval > 1500) return BEMF_FILTER_MAX;
    if (interval > 600)  return 8;
    if (interval > 250)  return 5;
    if (interval > 100)  return 3;
    return BEMF_FILTER_MIN;
}

/* ======================================================================
 * Phase output for one step
 * Order matters: float first, then low, then PWM -> no shoot-through
 * window with two high sides or a phase in transition.
 * ====================================================================== */
void comm_set_step(uint8_t step)
{
    const step_def_t *s = &k_steps[step];

    hal_phase_float(s->flt);
    hal_phase_low(s->low);
    hal_phase_pwm(s->pwm);

    hal_comp_select_input(s->flt);
    g_comm.rising = g_comm.forward ? s->rising_fwd : !s->rising_fwd;
}

/* ======================================================================
 * Commutate: advance step, reconfigure bridge + comparator.
 * Called from the commutation timer ISR (running) or directly (startup).
 * ====================================================================== */
void comm_commutate(void)
{
    uint8_t st = g_comm.step;

    if (g_comm.forward) {
        st = (st >= 6) ? 1 : (uint8_t)(st + 1);
    } else {
        st = (st <= 1) ? 6 : (uint8_t)(st - 1);
    }
    g_comm.step = st;

    hal_comp_irq_disable();
    comm_set_step(st);

    g_comm.zc_found = false;

    /* In interrupt mode, arm comparator after a blanking period.
     * Blanking is handled by the commutation timer: we schedule a short
     * blank and enable the IRQ in comm_timer_isr's "blank" phase. */
    if (!g_comm.old_routine) {
        hal_comp_irq_clear();
        hal_comp_irq_enable(g_comm.rising);
    }
}

/* ======================================================================
 * Zero-cross accepted: update timing, schedule the next commutation.
 * Shared by the IRQ path and the polling path.
 * ====================================================================== */
static void zero_cross_accepted(void)
{
    uint32_t now = hal_interval_timer_read();

    g_comm.last_zc_time = g_comm.this_zc_time;
    g_comm.this_zc_time = now;

    uint32_t raw = now - g_comm.last_zc_time;   /* wraps cleanly in uint32 */
    hal_interval_timer_reset();
    g_comm.this_zc_time = 0