# AM32-Style 4-in-1 ESC: Commutation State Machine and BEMF Zero-Crossing Detection

This is an original, AM32-architecture-compatible implementation. It is not a copy of the GPL AM32 source. The design is:

- One `motor_t` instance per ESC channel (4 total).
- A hardware abstraction layer (HAL), so the same logic runs on STM32F051, G071 or AT32F421 targets.
- A hybrid detection scheme:
  - **Polled zero-crossing** at low eRPM. This is robust and filtered.
  - **Comparator-interrupt zero-crossing** at high eRPM. This has low latency.
- Demagnetization blanking, timing advance, and a desync watchdog.

---

## `esc_hal.h`: hardware interface (implement per target)

```c
#ifndef ESC_HAL_H
#define ESC_HAL_H
#include <stdint.h>
#include <stdbool.h>

#define NUM_MOTORS 4

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

/* Gate drive control per motor */
void hal_phase_pwm(uint8_t motor, phase_t ph);     /* high side PWM (complementary if enabled) */
void hal_phase_low(uint8_t motor, phase_t ph);     /* low side fully on */
void hal_phase_float(uint8_t motor, phase_t ph);   /* both FETs off */
void hal_all_float(uint8_t motor);

/* BEMF comparator: selects floating phase vs. virtual neutral */
void hal_comp_select(uint8_t motor, phase_t ph);
bool hal_comp_output(uint8_t motor);               /* true = phase above neutral */
void hal_comp_irq_enable(uint8_t motor, bool rising);
void hal_comp_irq_disable(uint8_t motor);

/* Per-motor commutation timer, 1 tick = 0.5 us (2 MHz) */
uint16_t hal_timer_now(uint8_t motor);
void     hal_timer_reset(uint8_t motor);
void     hal_comm_timer_schedule(uint8_t motor, uint16_t ticks); /* one-shot -> esc_comm_timer_isr */
void     hal_comm_timer_cancel(uint8_t motor);

void     hal_set_duty(uint8_t motor, uint16_t duty); /* 0..2000 */
#endif
```

---

## `esc_comm.h`

```c
#ifndef ESC_COMM_H
#define ESC_COMM_H
#include "esc_hal.h"

typedef enum {
    MSTATE_STOPPED = 0,
    MSTATE_STARTUP,      /* open-loop / polled, waiting for first valid ZCs */
    MSTATE_RUNNING_POLL, /* closed-loop, polled ZC detection */
    MSTATE_RUNNING_IRQ,  /* closed-loop, comparator interrupt ZC */
    MSTATE_DESYNC
} motor_state_t;

typedef enum {
    ZC_WAIT_BLANK = 0,   /* demag blanking after commutation */
    ZC_SEARCHING,        /* looking for crossing */
    ZC_FOUND             /* commutation scheduled */
} zc_phase_t;

typedef struct {
    /* config */
    bool     reversed;
    uint8_t  advance_deg;        /* 0..30 electrical degrees, typ 15-22 */
    uint8_t  filter_base;        /* min consecutive samples for polled ZC */

    /* runtime */
    volatile motor_state_t state;
    volatile zc_phase_t    zc_state;
    volatile uint8_t  step;            /* 1..6 */
    volatile uint16_t comm_interval;   /* ticks per 60 electrical degrees (avg) */
    volatile uint16_t last_zc_time;
    volatile uint16_t zc_timeout;
    volatile uint8_t  filter_level;
    volatile uint8_t  filter_count;
    volatile uint16_t good_zc_count;
    volatile uint16_t bad_zc_count;
    volatile uint32_t erpm;
    uint16_t duty;
} motor_t;

extern motor_t motors[NUM_MOTORS];

void esc_init(uint8_t m);
void esc_start(uint8_t m);
void esc_stop(uint8_t m);
void esc_poll_zc(uint8_t m);          /* call from fast loop (>=20 kHz) */
void esc_comp_isr(uint8_t m);         /* comparator EXTI handler */
void esc_comm_timer_isr(uint8_t m);   /* commutation one-shot handler */
void esc_watchdog_1ms(uint8_t m);
#endif
```

---

## `esc_comm.c`

```c
#include "esc_comm.h"

motor_t motors[NUM_MOTORS];

/* ---------------- Commutation table ----------------
 * Forward sequence. Each step: PWM-high phase, low phase, floating phase,
 * and the expected BEMF edge on the floating phase (true = rising).
 */
typedef struct {
    phase_t hi, lo, fl;
    bool    rising;
} comm_step_t;

static const comm_step_t comm_table[6] = {
    /* step1 */ { PHASE_A, PHASE_B, PHASE_C, false },
    /* step2 */ { PHASE_A, PHASE_C, PHASE_B, true  },
    /* step3 */ { PHASE_B, PHASE_C, PHASE_A, false },
    /* step4 */ { PHASE_B, PHASE_A, PHASE_C, true  },
    /* step5 */ { PHASE_C, PHASE_A, PHASE_B, false },
    /* step6 */ { PHASE_C, PHASE_B, PHASE_A, true  },
};

/* Tunables */
#define IRQ_MODE_ENTER_INTERVAL   400   /* ticks (200us/60deg) -> switch to IRQ ZC */
#define IRQ_MODE_EXIT_INTERVAL    600   /* hysteresis */
#define MIN_COMM_INTERVAL         40    /* hard limit, ~ 250k eRPM */
#define MAX_COMM_INTERVAL         20000 /* below this speed -> desync */
#define STARTUP_GOOD_ZC           12
#define DESYNC_BAD_LIMIT          8
#define BLANK_PERCENT             25    /* % of interval for demag blanking */

/* ---------------- Helpers ---------------- */

static inline const comm_step_t *cur_step(const motor_t *mt)
{
    return &comm_table[mt->step - 1];
}

/* Expected comparator level once ZC has happened */
static inline bool zc_target_level(const motor_t *mt)
{
    bool r = cur_step(mt)->rising;
    return mt->reversed ? !r : r;
}

static void apply_step(uint8_t m)
{
    motor_t *mt = &motors[m];
    const comm_step_t *s = cur_step(mt);

    /* Break-before-make: float the phase that is about to float first */
    hal_phase_float(m, s->fl);
    hal_phase_low(m, s->lo);
    hal_phase_pwm(m, s->hi);

    hal_comp_select(m, s->fl);
}

static void advance_step(motor_t *mt)
{
    if (!mt->reversed) {
        mt->step = (mt->step >= 6) ? 1 : mt->step + 1;
    } else {
        mt->step = (mt->step <= 1) ? 6 : mt->step - 1;
    }
}

/* Filter strength scales with speed: more samples when slow, few when fast */
static uint8_t compute_filter_level(const motor_t *mt)
{
    uint16_t ci = mt->comm_interval;
    uint8_t f;
    if      (ci > 4000) f = mt->filter_base + 10;
    else if (ci > 2000) f = mt->filter_base + 6;
    else if (ci > 1000) f = mt->filter_base + 3;
    else if (ci > 500)  f = mt->filter_base + 1;
    else                f = mt->filter_base;
    if (mt->state == MSTATE_STARTUP) f += 4;
    return f;
}

/* ---------------- Commutation ---------------- */

static void commutate(uint8_t m)
{
    motor_t *mt = &motors[m];

    advance_step(mt);
    apply_step(m);

    mt->zc_state     = ZC_WAIT_BLANK;
    mt->filter_count = 0;
    mt->filter_level = compute_filter_level(mt);

    /* Switch between polled and IRQ detection with hysteresis */
    if (mt->state == MSTATE_RUNNING_POLL &&
        mt->comm_interval < IRQ_MODE_ENTER_INTERVAL) {
        mt->state = MSTATE_RUNNING_IRQ;
    } else if (mt->state == MSTATE_RUNNING_IRQ &&
               mt->comm_interval > IRQ_MODE_EXIT_INTERVAL) {
        mt->state = MSTATE_RUNNING_POLL;
        hal_comp_irq_disable(m);
    }

    /* Schedule end of demag blanking using the commutation timer */
    uint16_t blank = (uint32_t)mt->comm_interval * BLANK_PERCENT / 100;
    if (blank < 8) blank = 8;
    hal_comm_timer_schedule(m, blank);
}

/* Called when a zero crossing is confirmed (polled