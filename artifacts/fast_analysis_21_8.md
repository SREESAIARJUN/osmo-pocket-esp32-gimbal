# AM32-Style Commutation State Machine & BEMF Zero-Crossing Detection

Below is a self-contained C implementation modeled on the AM32 architecture: six-step trapezoidal commutation, comparator-based BEMF zero-cross detection, timing advance, blanking, filtering, open-loop startup and desync recovery.

**Assumptions:**
- I treated "Cycle 21" as a revision label. If it refers to a specific requirement, tell me and I'll adjust.
- On most 4-in-1 boards, each motor has its own MCU running identical firmware. This code keeps all state in a `motor_t` context, so it works either way.
- Hardware access goes through a thin HAL layer that you map to your MCU (STM32F051/G071/AT32F421, etc.).

---

## `bemf_commutation.h`

```c
#ifndef BEMF_COMMUTATION_H
#define BEMF_COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define COMM_TIMER_TICK_NS        500u     /* commutation timer: 2 MHz -> 0.5 us/tick   */
#define POLLING_MODE_THRESHOLD    2000u    /* comm interval (ticks) above which we poll  */
#define STARTUP_ZC_REQUIRED       20u      /* good zero-crosses before closed loop       */
#define STARTUP_INITIAL_INTERVAL  20000u   /* open-loop first step period (ticks)        */
#define STARTUP_MIN_INTERVAL      4000u    /* open-loop fastest step period              */
#define STARTUP_RAMP_DIVISOR      16u      /* open-loop acceleration rate                */
#define ZC_TIMEOUT_TICKS          60000u   /* no ZC in this long -> desync               */
#define MAX_INTERVAL_CHANGE_PCT   50u      /* sudden period jump -> desync               */
#define MIN_FILTER_LEVEL          2u
#define MAX_FILTER_LEVEL          12u
#define DEFAULT_ADVANCE_LEVEL     2u       /* 0..4 -> 0, 7.5, 15, 22.5, 30 elec. degrees */

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_ALIGN,
    MOTOR_OPEN_LOOP,
    MOTOR_CLOSED_LOOP,
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    /* State machine */
    volatile motor_state_t state;
    volatile uint8_t  step;                 /* 1..6                              */
    bool              reversed;

    /* Timing (all in comm-timer ticks) */
    volatile uint16_t last_zc_time;
    volatile uint32_t comm_interval;        /* filtered 60-deg electrical period */
    volatile uint32_t last_comm_interval;
    volatile uint16_t waitTime;             /* ZC -> commutation delay           */
    volatile uint16_t blank_ticks;

    /* Zero-cross detection */
    volatile bool     zc_found;
    volatile bool     rising;               /* expected BEMF edge on float phase */
    volatile bool     polling_mode;
    uint8_t           filter_level;
    uint8_t           advance_level;
    volatile uint16_t zc_good_count;
    volatile uint16_t ticks_since_comm;

    /* Stats / diagnostics */
    volatile uint32_t desync_count;
    volatile uint32_t total_zc;
} motor_t;

/* Public API */
void motor_init(motor_t *m);
void motor_start(motor_t *m);
void motor_stop(motor_t *m);

/* ISR hooks — call these from your vector handlers */
void bemf_comparator_isr(motor_t *m);     /* COMP edge interrupt          */
void commutation_timer_isr(motor_t *m);   /* comm timer compare interrupt */
void pwm_period_tick(motor_t *m);         /* called each PWM period (~24-48 kHz) */
void control_loop_1khz(motor_t *m);       /* slow loop: startup, desync   */

#endif
```

---

## `hal_esc.h` (port this to your MCU)

```c
#ifndef HAL_ESC_H
#define HAL_ESC_H
#include <stdint.h>
#include <stdbool.h>
#include "bemf_commutation.h"

/* Gate drive */
void     hal_phase_pwm(phase_t p);      /* high side PWM, complementary low (sync rect.) */
void     hal_phase_low(phase_t p);      /* low side fully on                              */
void     hal_phase_float(phase_t p);    /* both FETs off                                  */
void     hal_all_off(void);

/* Comparator */
void     hal_comp_select(phase_t p);    /* route floating phase vs virtual neutral to COMP */
bool     hal_comp_output(void);         /* true = phase above neutral                      */
void     hal_comp_irq_enable(bool rising_edge);
void     hal_comp_irq_disable(void);
void     hal_comp_irq_clear(void);

/* Commutation timer (free-running 16-bit) */
uint16_t hal_comm_timer_now(void);
void     hal_comm_timer_schedule(uint16_t delay_ticks); /* fire compare IRQ after delay */
void     hal_comm_timer_irq_disable(void);

/* Critical sections */
void     hal_irq_disable(void);
void     hal_irq_enable(void);
#endif
```

---

## `bemf_commutation.c`

```c
#include "bemf_commutation.h"
#include "hal_esc.h"

/* ------------------------------------------------------------------------
 * Six-step commutation table.
 * For each step: which phase gets PWM (high), which is held low,
 * which floats (BEMF sensed), and the expected BEMF edge on the float.
 *
 * Step  High  Low  Float  BEMF edge
 *  1     A     B    C     falling
 *  2     A     C    B     rising
 *  3     B     C    A     falling
 *  4     B     A    C     rising
 *  5     C     A    B     falling
 *  6     C     B    A     rising
 * ---------------------------------------------------------------------- */
typedef struct {
    phase_t high;
    phase_t low;
    phase_t floating;
    bool    bemf_rising;
} comm_step_t;

static const comm_step_t comm_table[6] = {
    { PHASE_A, PHASE_B, PHASE_C, false },
    { PHASE_A, PHASE_C, PHASE_B, true  },
    { PHASE_B, PHASE_C, PHASE_A, false },
    { PHASE_B, PHASE_A, PHASE_C, true  },
    { PHASE_C, PHASE_A, PHASE_B, false },
    { PHASE_C, PHASE_B, PHASE_A, true  },
};

/* ======================================================================== */
/*                       Low-level step application                         */
/* ======================================================================== */

static void apply_step(motor_t *m)
{
    const comm_step_t *s = &comm_table[m->step - 1];

    /* Float first to avoid shoot-through between transitions */
    hal_phase_float(s->floating);
    hal_phase_low(s->low);
    hal_phase_pwm(s->high);

    hal_comp_select(s->floating);

    /* When reversed we walk the table backwards, which inverts the edge sense */
    m->rising = m->reversed ? !s->bemf_rising : s->bemf_rising;
}

static void advance_step(motor_t *m)
{
    if (!m->reversed) {
        m->step = (m->step >= 6) ? 1 : (uint8_t)(m->step + 1);
    } else {
        m->step = (m->step <= 1) ? 6 : (uint8_t)(m->step - 1);
    }
}

/* ======================================================================== */
/*                          Commutation core                                 */
/* ======================================================================== */

/* Dynamic comparator filter: more samples at low speed (noisy, long
 * intervals), fewer at high speed where the window is tight.            */
static void update_filter_level(motor_t *m)
{
    uint32_t ci = m->comm_interval;
    uint8_t f;

    if      (ci > 8000) f = MAX_FILTER_LEVEL;
    else if (ci > 4000) f = 8;
    else if (ci > 2000) f = 5;
    else if (ci > 800)  f = 3;
    else                f = MIN_FILTER_LEVEL;

    m->filter_level = f;
}

/* Blanking: ignore the comparator right after commutation while the
 * freewheeling current from the previous step demagnetizes the winding
 * (the float phase is clamped to a rail by the body diode during this). */
static uint16_t compute_blanking(const motor_t *m)
{
    uint32_t b = m->comm_interval / 8u;      /* ~7.5 electrical deg */
    if (b < 20u)   b = 20u;                  /* >= 10 us            */
    if (b > 2000u) b = 2000u;
    return (uint16_t)b;
}

static void commutate(motor_t *m)
{
    hal_comp_irq_disable();

    advance_step(m);
    apply_step(m);

    m->zc_found         = false;
    m->tic