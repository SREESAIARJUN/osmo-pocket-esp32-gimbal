# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

**Assumption about "Cycle 16":** I don't have a prior cycle to continue from, so this is a standalone, self-contained iteration. Paste your previous cycle's code if you want this one to match it exactly.

**Scope:** This is an original implementation modeled on AM32's architecture. It is not a copy of the GPL source.

**Target:** STM32F0/G0-class MCUs. Each motor on a 4-in-1 board has its own MCU running one instance of this code. All hardware access goes through a small HAL layer, so porting to other parts (AT32F421, GD32E230, etc.) only means rewriting `bldc_hal.h`.

---

## `bldc_hal.h`: Hardware Abstraction (port this per target)

```c
#ifndef BLDC_HAL_H
#define BLDC_HAL_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

/* Phase driver modes */
void hal_phase_pwm(phase_t p);      /* complementary PWM on high/low (or high-side PWM only) */
void hal_phase_low(phase_t p);      /* low-side FET fully on, high off               */
void hal_phase_float(phase_t p);    /* both FETs off -> phase available for BEMF      */

/* Comparator: non-inverting input = floating phase, inverting = virtual neutral */
void hal_comp_select_phase(phase_t p);
bool hal_comp_output(void);         /* raw comparator output level                    */
void hal_comp_irq_enable(bool rising);  /* arm EXTI on rising or falling edge          */
void hal_comp_irq_disable(void);

/* Free-running interval timer (1 tick = 0.5 us typical, 16-bit wraps handled) */
uint16_t hal_interval_timer_get(void);
void     hal_interval_timer_reset(void);

/* One-shot commutation timer: fires commutation_timer_isr() after 'ticks' */
void hal_com_timer_schedule(uint16_t ticks);
void hal_com_timer_stop(void);

/* Critical sections */
void hal_irq_disable(void);
void hal_irq_enable(void);
#endif
```

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,     /* open-loop / polled ZC at very low speed */
    MOTOR_RUNNING,      /* closed-loop, interrupt-driven ZC         */
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    /* configuration */
    bool     reversed;             /* spin direction                          */
    uint8_t  timing_advance;       /* 0..4  -> 0, 7.5, 15, 22.5, 30 degrees   */
    uint16_t min_interval;         /* ticks: protects against max-eRPM glitch */
    uint16_t max_interval;         /* ticks: below this speed -> desync/stop  */

    /* runtime */
    volatile motor_state_t state;
    volatile uint8_t  step;               /* 1..6 */
    volatile bool     rising;             /* expected BEMF edge on floating phase */
    volatile uint16_t commutation_interval;    /* filtered 60-deg period */
    volatile uint16_t last_zc_time;
    volatile uint16_t this_zc_time;
    volatile uint16_t advance_ticks;
    volatile uint16_t waitTime;           /* ZC -> commutation delay */
    volatile uint8_t  zc_found;
    volatile uint8_t  good_zc_count;      /* consecutive valid ZCs */
    volatile uint8_t  bad_zc_count;
    volatile uint8_t  filter_level;       /* comparator re-sample count */
    volatile uint16_t zc_timeout_counter;
    volatile uint32_t erpm;               /* informational, e-RPM */
} motor_t;

extern motor_t motor;

void commutation_init(void);
void commutate(void);                     /* advance one step and apply */
void comp_zero_cross_isr(void);           /* comparator EXTI handler */
void commutation_timer_isr(void);         /* one-shot timer handler */
void bemf_poll_low_speed(void);           /* call from fast loop (~20 kHz) when STARTING */
void commutation_watchdog_tick(void);     /* call at 1 kHz-ish for timeouts */
#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "bldc_hal.h"

motor_t motor;

/* ------------------------------------------------------------------ */
/* Commutation table                                                  */
/* For each step: which phase is PWM (high), which is LOW, which      */
/* floats, and the BEMF edge direction on the floating phase for      */
/* forward rotation.                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    phase_t high;
    phase_t low;
    phase_t flt;
    bool    rising;
} step_def_t;

static const step_def_t step_table[7] = {
    /* idx 0 unused */
    { PHASE_A, PHASE_A, PHASE_A, false },
    /* 1 */ { PHASE_A, PHASE_B, PHASE_C, true  },  /* A+ B-  C float, rising  */
    /* 2 */ { PHASE_C, PHASE_B, PHASE_A, false },  /* C+ B-  A float, falling */
    /* 3 */ { PHASE_C, PHASE_A, PHASE_B, true  },  /* C+ A-  B float, rising  */
    /* 4 */ { PHASE_B, PHASE_A, PHASE_C, false },  /* B+ A-  C float, falling */
    /* 5 */ { PHASE_B, PHASE_C, PHASE_A, true  },  /* B+ C-  A float, rising  */
    /* 6 */ { PHASE_A, PHASE_C, PHASE_B, false },  /* A+ C-  B float, falling */
};

/* ------------------------------------------------------------------ */
/* Tunables                                                           */
/* ------------------------------------------------------------------ */
#define FILTER_LEVEL_SLOW     12   /* comparator samples at low eRPM  */
#define FILTER_LEVEL_FAST      2   /* at high eRPM                    */
#define BLANKING_PERCENT      25   /* % of interval ignored after commutation (demag) */
#define GOOD_ZC_TO_RUN        20   /* consecutive ZCs before entering closed loop */
#define BAD_ZC_LIMIT          10   /* consecutive bad ZCs -> desync */
#define INTERVAL_TO_RUN     4000   /* ticks: below this, use interrupt ZC */
#define ZC_TIMEOUT_MS        200

/* ------------------------------------------------------------------ */
/* Apply phase outputs for current step                               */
/* Order matters: float first, then low, then PWM -> avoids shoot-through
 * windows on drivers without hardware dead-time interlock.           */
/* ------------------------------------------------------------------ */
static void apply_step(uint8_t step)
{
    const step_def_t *s = &step_table[step];

    hal_phase_float(s->flt);
    hal_phase_low(s->low);
    hal_phase_pwm(s->high);

    hal_comp_select_phase(s->flt);

    /* Reversing direction walks the table backwards, which inverts
     * the BEMF slope on the floating phase. */
    motor.rising = motor.reversed ? !s->rising : s->rising;
}

/* ------------------------------------------------------------------ */
/* Advance step and commutate                                         */
/* ------------------------------------------------------------------ */
void commutate(void)
{
    if (!motor.reversed) {
        motor.step = (motor.step >= 6) ? 1 : motor.step + 1;
    } else {
        motor.step = (motor.step <= 1) ? 6 : motor.step - 1;
    }

    apply_step(motor.step);

    motor.zc_found = 0;
    /* Reset the interval timer origin for blanking on this step. */
}

/* ------------------------------------------------------------------ */
/* Interval filtering and commutation scheduling                      */
/* ------------------------------------------------------------------ */
static inline void update_interval(uint16_t measured)
{
    /* Simple IIR: new = (3*old + measured) / 4  (fast but smooth). */
    uint32_t avg = ((uint32_t)motor.commutation_interval * 3u + measured) >> 2;
    if (avg < motor.min_interval) avg = motor.min_interval;
    motor.commutation_interval = (uint16_t)avg;

    /* Advance: timing_advance * 7.5deg; 60deg = one interval.
     * advance_ticks = interval * (timing*7.5)/60 = interval * timing / 8 */
    motor.advance_ticks = (uint16_t)((avg * motor.timing_advance) >> 3);

    /* Nominal delay ZC -> commutation is 30 deg = interval/2 */
    uint16_t half = (uint16_t)(avg >> 1);
    motor.waitTime = (half > motor.advance_ticks) ? (half - motor.advance_ticks) : 1;

    /* Adaptive comparator filter */
    motor.filter_level = (avg < 300) ? FILTER_LEVEL_FAST :
                         (avg < 1500) ? 5 : FILTER_LEVEL_SLOW;

    /* eRPM: 6 intervals per electrical rev, 0.5us ticks -> 2,000,000 ticks/s */
    motor.erpm = (avg > 0) ? (120000000UL / (6UL * avg)) : 0;  /* RPM*... e-RPM */
}

/* Validate the edge by re-sampling the comparator several times. */
static bool comparator_filter