# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

I've read "Cycle 30" as the standard 30° electrical delay between a BEMF zero-crossing and the next commutation. The code below is an original, hardware-abstracted implementation modeled on AM32's architecture:

- Comparator interrupts for zero-crossing detection at speed, with polled detection at low RPM.
- Filtered comparator reads.
- Averaged commutation intervals.
- Timing advance.
- Desync recovery.

It is written per-ESC, so a 4-in-1 board runs one instance per MCU. Typically each of the 4 channels has its own MCU running this firmware.

---

## `bldc_hal.h`: hardware abstraction (implement per MCU, e.g. STM32F051/G071/AT32F421)

```c
#ifndef BLDC_HAL_H
#define BLDC_HAL_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

/* Gate drive control for one half-bridge */
void hal_phase_pwm(phase_t p);     /* high-side PWM (complementary if comp_pwm) */
void hal_phase_low(phase_t p);     /* low-side fully on                          */
void hal_phase_float(phase_t p);   /* both FETs off (Hi-Z), BEMF sensing phase   */

/* Comparator: + input = floating phase, - input = virtual neutral */
void hal_comp_select(phase_t floating_phase);
bool hal_comp_output(void);                 /* true = phase > neutral */
void hal_comp_irq_enable(bool rising_edge); /* edge on COMP/EXTI line  */
void hal_comp_irq_disable(void);

/* Free-running interval timer, 1 tick = 0.5 us (e.g. 48 MHz / 24) */
uint16_t hal_interval_timer_read(void);
void     hal_interval_timer_reset(void);

/* One-shot commutation timer */
void hal_comm_timer_start(uint16_t ticks); /* fires bldc_comm_timer_isr() */
void hal_comm_timer_stop(void);

void hal_all_phases_off(void);
#endif
```

---

## `bldc_commutation.h`

```c
#ifndef BLDC_COMMUTATION_H
#define BLDC_COMMUTATION_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint8_t  step;               /* 1..6                                   */
    volatile bool     forward;            /* direction                              */
    volatile bool     rising;             /* expected BEMF edge for current step    */
    volatile bool     running;
    volatile bool     zc_found;
    volatile bool     old_routine;        /* true = polling (low speed / startup)   */

    volatile uint16_t thiszctime;
    volatile uint16_t commutation_interval;    /* averaged, timer ticks (60 deg)    */
    volatile uint32_t commutation_intervals[6];
    volatile uint32_t e_com_time;              /* sum of last 6 = 1 e-revolution    */

    volatile uint16_t advance;            /* computed advance in ticks              */
    uint8_t           advance_level;      /* 0..4 -> 0, 7.5, 15, 22.5, 30 deg       */
    uint8_t           filter_level;       /* comparator consensus count             */
    uint8_t           bemf_timeout;       /* max steps without ZC before desync     */

    volatile uint16_t bemf_counter;       /* consecutive good ZCs                   */
    volatile uint8_t  zc_timeout_count;
    volatile bool     desync;
} bldc_state_t;

extern bldc_state_t bldc;

void bldc_init(void);
void bldc_commutate(void);
void bldc_set_step(uint8_t step);
void bldc_comp_isr(void);          /* call from comparator/EXTI IRQ */
void bldc_comm_timer_isr(void);    /* call from commutation timer IRQ */
void bldc_poll_zero_cross(void);   /* call in main loop / fast tick when old_routine */
void bldc_zc_timeout_check(void);  /* call ~every 1 ms */

#endif
```

---

## `bldc_commutation.c`

```c
#include "bldc_commutation.h"
#include "bldc_hal.h"

bldc_state_t bldc;

/* ---------------- Tunables ---------------- */
#define MIN_COMM_INTERVAL      40u     /* ticks, ~ max eRPM guard              */
#define MAX_COMM_INTERVAL      20000u  /* below this speed -> polling mode     */
#define POLL_MODE_THRESHOLD    2000u   /* switch to IRQ ZC when faster          */
#define POLL_MODE_HYST         2500u
#define BLANKING_DIV           8u      /* ignore comparator for interval/8 after comm */
#define ZC_TIMEOUT_STEPS       12u

/*
 * 6-step commutation table.
 *   high  : phase driven with PWM
 *   low   : phase held low
 *   flt   : floating phase (BEMF sensed)
 *   rising: BEMF on floating phase crosses neutral going up
 */
typedef struct { phase_t high, low, flt; bool rising; } comm_step_t;

static const comm_step_t comm_table[7] = {
    /* 0 unused */ { PHASE_A, PHASE_B, PHASE_C, true  },
    /* 1 */        { PHASE_A, PHASE_B, PHASE_C, true  },  /* AB, C rising  */
    /* 2 */        { PHASE_C, PHASE_B, PHASE_A, false },  /* CB, A falling */
    /* 3 */        { PHASE_C, PHASE_A, PHASE_B, true  },  /* CA, B rising  */
    /* 4 */        { PHASE_B, PHASE_A, PHASE_C, false },  /* BA, C falling */
    /* 5 */        { PHASE_B, PHASE_C, PHASE_A, true  },  /* BC, A rising  */
    /* 6 */        { PHASE_A, PHASE_C, PHASE_B, false },  /* AC, B falling */
};

/* ---------------- Phase drive ---------------- */
void bldc_set_step(uint8_t step)
{
    const comm_step_t *s = &comm_table[step];

    /* Float first to avoid shoot-through during transition */
    hal_phase_float(s->flt);
    hal_phase_low(s->low);
    hal_phase_pwm(s->high);
}

/* Select comparator input and expected edge for the current step.
 * Reversing direction inverts the expected BEMF slope. */
static void bldc_change_compinput(void)
{
    const comm_step_t *s = &comm_table[bldc.step];
    hal_comp_select(s->flt);
    bldc.rising = bldc.forward ? s->rising : !s->rising;
}

/* ---------------- Interval averaging ---------------- */
static void bldc_update_interval(uint16_t zc_time)
{
    uint8_t idx = bldc.step - 1;

    bldc.e_com_time -= bldc.commutation_intervals[idx];
    bldc.commutation_intervals[idx] = zc_time;
    bldc.e_com_time += zc_time;

    /* 2-tap IIR on the instantaneous interval, sanity clamp */
    uint32_t avg = ((uint32_t)bldc.commutation_interval + zc_time) >> 1;
    if (avg < MIN_COMM_INTERVAL) avg = MIN_COMM_INTERVAL;
    if (avg > MAX_COMM_INTERVAL) avg = MAX_COMM_INTERVAL;
    bldc.commutation_interval = (uint16_t)avg;

    /* Advance: interval = 60 deg, so advance_level*7.5deg = interval*level/8 */
    bldc.advance = (uint16_t)((bldc.commutation_interval * bldc.advance_level) >> 3);

    /* Mode switching with hysteresis */
    if (bldc.commutation_interval < POLL_MODE_THRESHOLD)      bldc.old_routine = false;
    else if (bldc.commutation_interval > POLL_MODE_HYST)      bldc.old_routine = true;
}

/* ---------------- Commutation ---------------- */
void bldc_commutate(void)
{
    if (bldc.forward) { if (++bldc.step > 6) bldc.step = 1; }
    else              { if (--bldc.step < 1) bldc.step = 6; }

    bldc_set_step(bldc.step);
    bldc_change_compinput();

    bldc.zc_found = false;
    hal_interval_timer_reset();

    if (!bldc.old_routine) {
        /* Blanking: let demag/flyback ringing settle before arming the IRQ.
         * Simple approach: arm immediately, the ISR rejects early edges. */
        hal_comp_irq_enable(!bldc.rising);  /* edge sense: see note in ISR */
    }
}

/* ---------------- Filtered comparator read ---------------- */
/* Returns true if the comparator agrees the crossing has happened
 * for 'filter_level' consecutive samples. */
static bool bldc_bemf_crossed(void)
{
    for (uint8_t i = 0; i < bldc.filter_level; i++) {
        bool out = hal_comp_output();
        /* Rising BEMF: comparator flips to high after ZC.
         * Falling BEMF: comparator flips to low after ZC. */
        if (out != bldc.rising) return false;
    }
    return true;
}

/* Common action once a valid zero-crossing is confirmed */
static void bld