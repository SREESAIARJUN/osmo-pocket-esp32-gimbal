# AM32-Style 6-Step Commutation & BEMF Zero-Crossing Detection

This is a clean-room implementation modeled on the AM32 architecture. It is not verbatim AM32 source. The target is an STM32G071/F051-class MCU.

On a 4-in-1 board, each motor usually has its own MCU running this code. The `HAL_*` hooks isolate the board-specific pin and timer mapping.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define MOTOR_POLES                 14
#define TIMING_ADVANCE_DEG          15      /* 0..30 electrical degrees            */
#define MIN_COMMUTATION_INTERVAL_US 40      /* ~250k eRPM ceiling                  */
#define MAX_COMMUTATION_INTERVAL_US 20000   /* below this => considered stalled    */
#define POLLING_MODE_THRESHOLD_US   2000    /* slower than this -> poll comparator */
#define INTERRUPT_MODE_THRESHOLD_US 1500    /* hysteresis back to interrupt mode   */
#define DEMAG_BLANKING_PERCENT      25      /* % of interval ignored after commutate */
#define ZC_FILTER_MAX               12
#define ZC_FILTER_MIN               3
#define BEMF_TIMEOUT_COMMUTATIONS   10      /* missed ZCs before desync declared    */
#define STARTUP_FORCED_INTERVAL_US  6000

typedef enum {
    PHASE_A = 0,
    PHASE_B = 1,
    PHASE_C = 2
} phase_t;

typedef enum {
    PHASE_STATE_FLOAT = 0,
    PHASE_STATE_PWM,        /* high side PWM (complementary low if enabled) */
    PHASE_STATE_LOW         /* low side fully on */
} phase_drive_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,
    MOTOR_RUNNING,
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    volatile uint8_t  step;               /* 1..6 */
    volatile bool     forward;
    volatile motor_state_t state;

    volatile uint32_t commutation_interval;   /* timer ticks (1 tick = 1 us) */
    volatile uint32_t last_zc_time;
    volatile uint32_t this_zc_time;
    volatile uint32_t advance_ticks;
    volatile uint32_t wait_ticks;

    volatile bool     rising;             /* expected BEMF edge on floating phase */
    volatile bool     polling_mode;
    volatile uint8_t  zc_filter_level;
    volatile uint8_t  bemf_timeout_count;
    volatile uint8_t  good_zc_count;      /* consecutive valid ZCs */
    volatile bool     zc_found;

    volatile uint16_t duty;               /* 0..TIMER_ARR */
    volatile uint32_t erpm;
} motor_ctx_t;

extern motor_ctx_t motor;

void commutation_init(void);
void motor_start(void);
void motor_stop(void);
void commutate(void);
void zero_cross_poll(void);         /* call from fast loop / 20 kHz tick   */
void comparator_isr(void);          /* call from COMP EXTI IRQ             */
void commutation_timer_isr(void);   /* call from COM timer update/CCx IRQ  */
void bemf_watchdog_tick(void);      /* call every ~1 ms                    */

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "hal_esc.h"   /* board-specific hooks, see bottom */

motor_ctx_t motor;

/* ---------------------------------------------------------------------------
 * Six-step commutation table (forward direction)
 *
 *  Step | High(PWM) | Low | Float | BEMF edge on float
 *  -----+-----------+-----+-------+-------------------
 *   1   |    A      |  B  |   C   |  falling
 *   2   |    C      |  B  |   A   |  rising
 *   3   |    C      |  A  |   B   |  falling
 *   4   |    B      |  A  |   C   |  rising
 *   5   |    B      |  C  |   A   |  falling
 *   6   |    A      |  C  |   B   |  rising
 *
 * Reverse is the same table walked backwards, with inverted edge polarity.
 * ------------------------------------------------------------------------- */
typedef struct {
    phase_t pwm_phase;
    phase_t low_phase;
    phase_t float_phase;
    bool    rising_fwd;
} comm_step_t;

static const comm_step_t comm_table[7] = {
    { 0 },                                         /* index 0 unused */
    { PHASE_A, PHASE_B, PHASE_C, false },
    { PHASE_C, PHASE_B, PHASE_A, true  },
    { PHASE_C, PHASE_A, PHASE_B, false },
    { PHASE_B, PHASE_A, PHASE_C, true  },
    { PHASE_B, PHASE_C, PHASE_A, false },
    { PHASE_A, PHASE_C, PHASE_B, true  },
};

/* ---------------------------------------------------------------------------
 * Low-level phase driving
 * ------------------------------------------------------------------------- */
static inline void set_phase(phase_t p, phase_drive_t d)
{
    switch (d) {
    case PHASE_STATE_FLOAT:
        HAL_phase_high_off(p);
        HAL_phase_low_off(p);
        break;
    case PHASE_STATE_PWM:
        HAL_phase_low_off(p);          /* break-before-make: low off first */
        HAL_phase_high_pwm(p);         /* complementary PWM w/ deadtime in HW */
        break;
    case PHASE_STATE_LOW:
        HAL_phase_high_off(p);
        HAL_phase_low_on(p);
        break;
    }
}

static void apply_step(uint8_t step)
{
    const comm_step_t *s = &comm_table[step];

    /* Float first to avoid shoot-through on transition */
    set_phase(s->float_phase, PHASE_STATE_FLOAT);
    set_phase(s->low_phase,   PHASE_STATE_LOW);
    set_phase(s->pwm_phase,   PHASE_STATE_PWM);

    /* Point comparator's + input at the floating phase, - at virtual neutral */
    HAL_comp_select_phase(s->float_phase);

    motor.rising = motor.forward ? s->rising_fwd : !s->rising_fwd;

    /* In interrupt mode, arm the comparator EXTI for the expected edge.
     * The comparator output is inverted vs. the BEMF edge on most AM32
     * boards; HAL_comp_set_edge() hides that detail. */
    if (!motor.polling_mode) {
        HAL_comp_set_edge(motor.rising);
    }
}

/* ---------------------------------------------------------------------------
 * Commutation: advance the state machine one step
 * ------------------------------------------------------------------------- */
void commutate(void)
{
    if (motor.forward) {
        motor.step = (motor.step >= 6) ? 1 : motor.step + 1;
    } else {
        motor.step = (motor.step <= 1) ? 6 : motor.step - 1;
    }

    HAL_comp_irq_disable();          /* ignore ringing during the switch */
    apply_step(motor.step);

    motor.zc_found = false;
    motor.bemf_timeout_count++;      /* cleared when a valid ZC arrives */

    /* Demagnetisation blanking: the floating phase's freewheel diode
     * conducts for a while after commutation and produces a false
     * crossing. Hold off detection for a fraction of the interval. */
    uint32_t blank = (motor.commutation_interval * DEMAG_BLANKING_PERCENT) / 100;
    if (blank < 10) blank = 10;

    HAL_interval_timer_reset();      /* free-running us timer used for ZC timing */
    HAL_blanking_timer_start(blank); /* fires blanking_done -> enable comp irq   */
}

/* Called by HAL when the blanking period expires */
void blanking_done_callback(void)
{
    if (!motor.polling_mode && motor.state != MOTOR_STOPPED) {
        HAL_comp_clear_pending();
        HAL_comp_irq_enable();
    }
}

/* ---------------------------------------------------------------------------
 * Zero-crossing handling (shared by ISR and polling paths)
 * ------------------------------------------------------------------------- */
static void on_zero_cross(void)
{
    uint32_t now = HAL_interval_timer_read();   /* us since last commutation */

    HAL_comp_irq_disable();
    motor.zc_found = true;
    motor.bemf_timeout_count = 0;

    /* Interval estimate: time from the last commutation to this ZC is
     * ~half a step (30 deg). Average with the previous value to smooth jitter.
     * A full step = 60 deg = 2 * (commutation->ZC) at steady state. */
    uint32_t measured = now * 2;
    motor.commutation_interval = (motor.commutation_interval + measured) >> 1;

    if (motor.commutation_interval < MIN_COMMUTATION_INTERVAL_US)
        motor.commutation_interval = MIN_COMMUTATION_INTERVAL_US;

    /* Advance: (interval / 60deg) * advance_deg */
    motor.advance_ticks = (motor.commutation_interval * TIMING_ADVANCE_DEG) / 60;

    /* Ideal commutation is 30 deg after ZC; subtract advance. */
    uint32_t half = motor.commutation_interval >> 1;
    motor.wait_ticks = (half > motor.advance_ticks) ? (half - motor.advance_ticks) : 1;

    HAL_commutation_timer_schedule(motor.wait_ticks);  /* -> commutation_timer_isr */

    if (motor.good_zc_count < 255) motor.good_zc_count++;

    /* Transition startup -> closed loop after enough consecutive good ZCs */
    if (motor.state == MOTOR_STARTING && motor.good_zc_count > 20) {
        motor