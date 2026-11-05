# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection (Cycle 8)

This is a self-contained C module modeled on the AM32 architecture:

- A 6-step commutation state machine.
- Comparator-based BEMF zero-crossing detection with an interrupt path for normal running.
- A polled path for startup and low speed.
- Timing advance, zero-cross filtering and desync protection.

I treated "Cycle 8" as revision 8 of this module. If you meant something specific, such as a particular AM32 release or target MCU, tell me and I'll adapt it.

Hardware access sits behind small inline functions at the top. You map those to your target, such as F051, G071 or AT32F421, and the core logic stays portable.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Configuration ---------- */
#define COMM_TIMER_HZ            1000000UL   /* commutation/interval timer: 1 MHz (1 us tick) */
#define MIN_COMMUTATION_INTERVAL 30          /* us, ~ upper eRPM limit guard              */
#define MAX_COMMUTATION_INTERVAL 20000       /* us, below this speed => polling mode       */
#define POLLING_MODE_THRESHOLD   2000        /* us, switch to comparator IRQ when faster   */
#define ZC_FILTER_MAX            12
#define ZC_FILTER_MIN            2
#define DESYNC_BAD_COUNT_LIMIT   48
#define DEFAULT_ADVANCE_LEVEL    2           /* 0..3 -> 0, 7.5, 15, 22.5 deg (x7.5)        */

/* ---------- Phase drive states ---------- */
typedef enum {
    PHASE_FLOAT = 0,   /* both FETs off, phase sensed by comparator */
    PHASE_PWM,         /* high side PWM (complementary if enabled)  */
    PHASE_LOW          /* low side held on                          */
} phase_state_t;

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_id_t;

/* ---------- Public state ---------- */
typedef struct {
    volatile uint8_t  step;              /* 1..6                                  */
    volatile bool     forward;           /* rotation direction                    */
    volatile bool     rising;            /* expected BEMF edge on floating phase  */
    volatile bool     running;
    volatile bool     old_routine;       /* true => polling mode (startup/slow)   */
    volatile bool     zc_found;

    volatile uint16_t commutation_interval;   /* smoothed, us (time per step)     */
    volatile uint16_t intervals[6];
    volatile uint16_t this_zc_time;
    volatile uint16_t last_zc_time;
    volatile uint16_t waitTime;               /* ZC -> commutation delay           */
    volatile uint16_t advance;

    volatile uint8_t  filter_level;
    volatile uint8_t  bemf_counts;           /* consecutive good ZCs              */
    volatile uint16_t bad_count;
    volatile uint8_t  advance_level;

    volatile uint32_t e_com_time;            /* sum of last 6 intervals = 1 erev   */
    volatile bool     desync;
} comm_state_t;

extern comm_state_t g_comm;

void comm_init(bool forward, uint8_t advance_level);
void comm_start(void);
void comm_stop(void);
void commutate(void);
void comStep(uint8_t step);
void changeCompInput(void);
void interruptRoutine(void);       /* call from comparator EXTI IRQ          */
void commutationTimerISR(void);    /* call from commutation one-shot timer   */
void getBemfState(void);           /* call from fast loop in polling mode    */
void PeriodElapsedCallback(void);

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"

comm_state_t g_comm;

/* ======================================================================
 *  HARDWARE ABSTRACTION – map these to your MCU
 * ====================================================================== */

/* Per-phase gate control. AM32 uses timer CCxE/CCxNE for PWM and GPIO
 * mode switching for LOW/FLOAT. Implement with your pin/timer macros. */
static inline void hw_phase_set(phase_id_t ph, phase_state_t st);

/* Comparator: select which phase is the inverting input vs. virtual
 * neutral, and which edge triggers the EXTI line. */
static inline void hw_comp_select_phase(phase_id_t ph);
static inline void hw_comp_set_edge(bool rising);
static inline void hw_comp_irq_enable(void);
static inline void hw_comp_irq_disable(void);
static inline bool hw_comp_output(void);          /* raw comparator output */

/* Interval timer: free-running 16-bit, 1 us tick */
static inline uint16_t hw_interval_timer_get(void);
static inline void     hw_interval_timer_reset(void);

/* One-shot commutation timer: fire after N us */
static inline void hw_com_timer_schedule(uint16_t us);
static inline void hw_com_timer_disable(void);

/* ---- Example STM32G071-ish implementation stubs (replace) ---- */
#ifdef AM32_EXAMPLE_HW
#include "stm32g0xx_ll_comp.h"
/* ... */
#endif

/* ======================================================================
 *  6-STEP TABLE
 *  step : A      B      C      floating  BEMF edge (forward)
 *   1   : PWM    LOW    FLOAT  C         falling
 *   2   : PWM    FLOAT  LOW    B         rising
 *   3   : FLOAT  PWM    LOW    A         falling
 *   4   : LOW    PWM    FLOAT  C         rising
 *   5   : LOW    FLOAT  PWM    B         falling
 *   6   : FLOAT  LOW    PWM    A         rising
 * ====================================================================== */
typedef struct {
    phase_state_t a, b, c;
    phase_id_t    floating;
    bool          rising_fwd;
} step_def_t;

static const step_def_t step_table[7] = {
    [0] = { PHASE_FLOAT, PHASE_FLOAT, PHASE_FLOAT, PHASE_A, false }, /* unused */
    [1] = { PHASE_PWM,   PHASE_LOW,   PHASE_FLOAT, PHASE_C, false },
    [2] = { PHASE_PWM,   PHASE_FLOAT, PHASE_LOW,   PHASE_B, true  },
    [3] = { PHASE_FLOAT, PHASE_PWM,   PHASE_LOW,   PHASE_A, false },
    [4] = { PHASE_LOW,   PHASE_PWM,   PHASE_FLOAT, PHASE_C, true  },
    [5] = { PHASE_LOW,   PHASE_FLOAT, PHASE_PWM,   PHASE_B, false },
    [6] = { PHASE_FLOAT, PHASE_LOW,   PHASE_PWM,   PHASE_A, true  },
};

/* ======================================================================
 *  PHASE DRIVE
 * ====================================================================== */
void comStep(uint8_t step)
{
    const step_def_t *s = &step_table[step];

    /* Turn OFF first to guarantee no shoot-through across transitions,
     * then turn ON. Order: float -> low -> pwm. */
    if (s->a == PHASE_FLOAT) hw_phase_set(PHASE_A, PHASE_FLOAT);
    if (s->b == PHASE_FLOAT) hw_phase_set(PHASE_B, PHASE_FLOAT);
    if (s->c == PHASE_FLOAT) hw_phase_set(PHASE_C, PHASE_FLOAT);

    if (s->a == PHASE_LOW) hw_phase_set(PHASE_A, PHASE_LOW);
    if (s->b == PHASE_LOW) hw_phase_set(PHASE_B, PHASE_LOW);
    if (s->c == PHASE_LOW) hw_phase_set(PHASE_C, PHASE_LOW);

    if (s->a == PHASE_PWM) hw_phase_set(PHASE_A, PHASE_PWM);
    if (s->b == PHASE_PWM) hw_phase_set(PHASE_B, PHASE_PWM);
    if (s->c == PHASE_PWM) hw_phase_set(PHASE_C, PHASE_PWM);
}

/* ======================================================================
 *  COMPARATOR INPUT SELECTION
 * ====================================================================== */
void changeCompInput(void)
{
    const step_def_t *s = &step_table[g_comm.step];

    hw_comp_select_phase(s->floating);

    /* Reversing direction flips the BEMF slope on the floating phase */
    g_comm.rising = g_comm.forward ? s->rising_fwd : !s->rising_fwd;

    /* AM32 convention: the EXTI edge is the comparator output transition
     * that corresponds to the zero cross. With phase on the inverting input,
     * a rising BEMF gives a falling comparator output. */
    hw_comp_set_edge(!g_comm.rising);
}

/* ======================================================================
 *  COMMUTATION
 * ====================================================================== */
static inline void advance_step(void)
{
    if (g_comm.forward) {
        g_comm.step = (g_comm.step >= 6) ? 1 : g_comm.step + 1;
    } else {
        g_comm.step = (g_comm.step <= 1) ? 6 : g_comm.step - 1;
    }
}

void commutate(void)
{
    advance_step();

    /* Record per-step interval and update electrical revolution time */
    g_comm.intervals[g_comm.step - 1] = g_comm.commutation_interval;
    g_comm.e_com_time = 0;
    for (int i = 0; i < 6; i++) g_comm.e_com_time += g_comm.intervals[i];

    comStep(g_comm.step);
    changeCompInput();

    /* Too many bad zero crosses since last good lock => drop to polling */
    if (g_comm.bad_count > DESYNC_BAD_COUNT_LIMIT) {
        g_comm.desync      = true;
        g_comm.old_routine = true;
        g_com