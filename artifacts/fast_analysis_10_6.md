# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

I don't have context for what "Cycle 10" refers to in your series, so this is a complete, standalone module. It follows the AM32 architecture:

- **Comparator-interrupt zero-crossing** at running speed.
- **Polled zero-crossing** at low speed and during startup.
- **30° electrical delay** with timing advance.
- **Blanking**, demag and desync protection.

It is an original implementation, not a copy of AM32 source. Hardware access is behind a small HAL so it can be ported to the F051, G071, AT32 or GD32 targets used on 4-in-1 boards. On a 4-in-1 board each motor normally has its own MCU, so you instantiate one `esc_motor_t` per MCU.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Phase identifiers ---------- */
typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

/* ---------- Tunables ---------- */
#define COMM_TIMER_HZ             1000000u   /* 1 MHz commutation timebase (1 us/tick) */
#define MIN_COMM_INTERVAL_US      40u        /* ~ upper eRPM limit guard               */
#define MAX_COMM_INTERVAL_US      20000u     /* below this speed -> polled mode        */
#define POLLED_MODE_THRESH_US     2000u      /* use polling above this interval        */
#define ZC_FILTER_MIN             2u         /* comparator re-reads at high speed      */
#define ZC_FILTER_MAX             12u        /* comparator re-reads at low speed       */
#define POLL_CONFIRM_COUNT        6u         /* consecutive samples needed (polled)    */
#define DESYNC_RATIO_NUM          2u         /* reject zc if interval < prev/2         */
#define DESYNC_RATIO_DEN          1u
#define BLANKING_MIN_US           6u
#define DEMAG_TIMEOUT_DIV         2u         /* demag if no zc within interval*1.5     */

/* ---------- Motor runtime state ---------- */
typedef struct {
    volatile uint8_t  step;            /* 1..6                                    */
    volatile bool     forward;         /* direction                               */
    volatile bool     rising;          /* expected BEMF edge on floating phase    */
    volatile bool     running;
    volatile bool     zc_irq_mode;     /* true = comparator interrupt mode        */

    volatile uint32_t last_zc_time;    /* timer ticks                             */
    volatile uint32_t comm_interval;   /* filtered interval between commutations  */
    volatile uint32_t zc_interval_raw;
    volatile uint32_t waitTime;        /* 30deg - advance                         */
    volatile uint32_t blank_ticks;

    uint8_t  advance_level;            /* 0..4 -> 0, 7.5, 15, 22.5, 30 deg        */
    uint8_t  zc_filter_level;
    uint8_t  poll_confirm;

    volatile uint32_t good_zc_count;
    volatile uint32_t bad_zc_count;
    volatile uint32_t demag_events;
    volatile bool     desync;

    uint16_t duty;                     /* 0..PWM_TOP                              */
} esc_motor_t;

extern esc_motor_t g_motor;

/* ---------- Public API ---------- */
void comm_init(esc_motor_t *m, bool forward, uint8_t advance_level);
void comm_start(esc_motor_t *m, uint32_t initial_interval_us);
void comm_stop(esc_motor_t *m);
void comm_commutate(esc_motor_t *m);       /* advance one step + reconfigure      */
void comm_poll_zero_cross(esc_motor_t *m); /* call from fast loop / 20 kHz tick   */
void comm_timer_isr(esc_motor_t *m);       /* commutation-timer compare ISR       */
void comm_comparator_isr(esc_motor_t *m);  /* comparator EXTI ISR                 */

/* ---------- HAL (implement per MCU) ---------- */
void     hal_phase_pwm(phase_t p);         /* complementary PWM on high side      */
void     hal_phase_low(phase_t p);         /* low side fully on                   */
void     hal_phase_float(phase_t p);       /* both FETs off                       */
void     hal_comp_select(phase_t floating);/* mux comparator + to floating phase  */
bool     hal_comp_output(void);            /* true = phase > virtual neutral      */
void     hal_comp_irq_enable(bool rising_edge);
void     hal_comp_irq_disable(void);
uint32_t hal_timer_now(void);              /* free-running 1 MHz counter (32-bit) */
void     hal_comm_timer_schedule(uint32_t delay_ticks);
void     hal_comm_timer_cancel(void);

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"

esc_motor_t g_motor;

/* -----------------------------------------------------------------------
 * Six-step table (forward). Each entry: high (PWM) phase, low phase,
 * floating phase, and whether floating-phase BEMF rises after this step.
 *
 *  step | PWM | LOW | FLOAT | BEMF on float
 *   1   |  A  |  B  |   C   |   rising
 *   2   |  A  |  C  |   B   |   falling
 *   3   |  B  |  C  |   A   |   rising
 *   4   |  B  |  A  |   C   |   falling
 *   5   |  C  |  A  |   B   |   rising
 *   6   |  C  |  B  |   A   |   falling
 * Reverse direction walks the table backwards; the edge polarity inverts.
 * --------------------------------------------------------------------- */
typedef struct {
    phase_t hi, lo, fl;
    bool    rising_fwd;
} comm_step_t;

static const comm_step_t k_steps[7] = {
    { PHASE_A, PHASE_A, PHASE_A, false }, /* index 0 unused */
    { PHASE_A, PHASE_B, PHASE_C, true  },
    { PHASE_A, PHASE_C, PHASE_B, false },
    { PHASE_B, PHASE_C, PHASE_A, true  },
    { PHASE_B, PHASE_A, PHASE_C, false },
    { PHASE_C, PHASE_A, PHASE_B, true  },
    { PHASE_C, PHASE_B, PHASE_A, false },
};

/* ---------------------------------------------------------------------- */
static inline uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Advance in 7.5 deg units. 60 deg electrical == one comm_interval,
 * so 7.5 deg == interval / 8. */
static inline uint32_t advance_ticks(const esc_motor_t *m)
{
    return (m->comm_interval >> 3) * m->advance_level;
}

/* Filter strength scales with speed: more samples at low eRPM where
 * noise per unit time is worse and time budget is larger. */
static inline uint8_t compute_filter_level(uint32_t interval)
{
    if (interval < 100)  return ZC_FILTER_MIN;
    if (interval < 400)  return 4;
    if (interval < 1000) return 7;
    return ZC_FILTER_MAX;
}

/* Blanking: ignore comparator right after commutation (ringing and
 * freewheel diode conduction / demag). Scales with interval and duty. */
static inline uint32_t compute_blanking(const esc_motor_t *m)
{
    uint32_t b = m->comm_interval >> 3;        /* ~7.5 deg */
    if (m->duty > 1500) b += m->comm_interval >> 4; /* extra at high load */
    return clamp_u32(b, BLANKING_MIN_US, m->comm_interval >> 1);
}

/* ---------------------------------------------------------------------- */
static void apply_step(esc_motor_t *m)
{
    const comm_step_t *s = &k_steps[m->step];

    /* Break-before-make: float the phase that is about to change first. */
    hal_phase_float(s->fl);
    hal_phase_low(s->lo);
    hal_phase_pwm(s->hi);

    m->rising = m->forward ? s->rising_fwd : !s->rising_fwd;
    hal_comp_select(s->fl);
}

void comm_init(esc_motor_t *m, bool forward, uint8_t advance_level)
{
    hal_comp_irq_disable();
    hal_comm_timer_cancel();

    m->step            = 1;
    m->forward         = forward;
    m->running         = false;
    m->zc_irq_mode     = false;
    m->advance_level   = advance_level > 4 ? 4 : advance_level;
    m->comm_interval   = MAX_COMM_INTERVAL_US;
    m->zc_filter_level = ZC_FILTER_MAX;
    m->poll_confirm    = 0;
    m->good_zc_count   = 0;
    m->bad_zc_count    = 0;
    m->demag_events    = 0;
    m->desync          = false;

    hal_phase_float(PHASE_A);
    hal_phase_float(PHASE_B);
    hal_phase_float(PHASE_C);
}

void comm_start(esc_motor_t *m, uint32_t initial_interval_us)
{
    m->comm_interval = clamp_u32(initial_interval_us,
                                 MIN_COMM_INTERVAL_