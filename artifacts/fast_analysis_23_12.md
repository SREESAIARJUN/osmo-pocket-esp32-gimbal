# AM32-Style Six-Step Commutation & BEMF Zero-Crossing Detection

This is a clean-room C implementation modeled on the AM32 architecture. It includes:

- the six-step commutation state machine
- comparator-based BEMF zero-crossing detection, with a polled mode for low speed and an interrupt mode for high speed
- 30° commutation delay with timing advance
- desync and stall protection

It targets a single MCU per motor, which is how 4-in-1 boards running AM32 are typically built. Each of the four MCUs runs this same code. Hardware specifics are isolated behind a small HAL, so you can port it to the F051, G071, AT32F421 or similar.

> I wasn't sure what "Cycle 23" refers to in your project, such as a sprint or revision tag. If it implies specific constraints, like a target MCU, PWM frequency or an existing codebase to match, tell me and I'll adapt it.

---

## `esc_hal.h`: hardware abstraction (implement per target)

```c
#ifndef ESC_HAL_H
#define ESC_HAL_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

/* --- Gate drive --- */
void hal_phase_pwm(phase_t p);      /* high side PWM, low side complementary (or off if not comp.) */
void hal_phase_low(phase_t p);      /* low side solid ON, high side OFF                              */
void hal_phase_float(phase_t p);    /* both FETs OFF (phase floating, used for BEMF sensing)       */

/* --- Comparator --- */
void hal_comp_select_input(phase_t p);  /* route floating phase to comp +, virtual neutral to comp - */
bool hal_comp_output(void);              /* raw comparator output level                              */
void hal_comp_exti_enable(bool rising);  /* enable comparator EXTI on chosen edge                    */
void hal_comp_exti_disable(void);
void hal_comp_exti_clear(void);

/* --- Timers --- */
uint16_t hal_interval_timer_read(void);   /* free-running 1 MHz (0.5 us on G071 if prescaled) */
void     hal_interval_timer_reset(void);
void     hal_com_timer_set(uint16_t ticks);/* one-shot commutation timer, fires com_timer_isr() */
void     hal_com_timer_enable_irq(void);
void     hal_com_timer_disable_irq(void);

/* --- Misc --- */
void hal_all_phases_float(void);
void hal_brake_full(void);

#endif
```

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H
#include <stdint.h>
#include <stdbool.h>

void esc_motor_init(void);
void esc_motor_start(void);
void esc_motor_stop(void);

/* Called from main loop (polled ZC at low speed) */
void esc_poll_zero_cross(void);

/* ISR hooks — wire these into the vector table */
void comp_exti_isr(void);      /* comparator edge interrupt     */
void com_timer_isr(void);      /* commutation delay elapsed     */
void ten_khz_isr(void);        /* 10 kHz housekeeping tick      */

/* Config */
extern volatile uint8_t  esc_dir_reversed;   /* 0 = forward, 1 = reverse        */
extern volatile uint8_t  esc_timing_advance; /* 0..3 -> 0, 7.5, 15, 22.5 deg    */
extern volatile uint16_t esc_duty;           /* set by input/throttle module     */
#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "esc_hal.h"

/* ------------------------------------------------------------------ */
/* Tunables                                                            */
/* ------------------------------------------------------------------ */
#define POLLING_MODE_THRESHOLD   2500u   /* e_com interval (us*?) above which we poll  */
#define POLLING_MODE_HYST         400u
#define MIN_FILTER_LEVEL            2u   /* comparator sample agreement count           */
#define MAX_FILTER_LEVEL           12u
#define ZC_TIMEOUT_TICKS        25000u   /* ~ commutation timeout -> desync             */
#define STARTUP_BEMF_GOOD          12u   /* consecutive good ZCs before closed loop     */
#define BEMF_TIMEOUT_LIMIT         10u   /* missed ZCs before declaring stall           */
#define MIN_COM_INTERVAL          100u   /* sanity clamp, rejects noise ZCs             */

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
volatile uint8_t  esc_dir_reversed   = 0;
volatile uint8_t  esc_timing_advance = 2;      /* 15 deg default */
volatile uint16_t esc_duty           = 0;

static volatile uint8_t  step              = 1;       /* 1..6                       */
static volatile bool     running           = false;
static volatile bool     polling_mode      = true;
static volatile bool     zc_expected_rising= false;
static volatile bool     old_routine       = true;    /* AM32 naming: polled path   */

static volatile uint16_t commutation_interval = 12500;/* last full 60-deg interval  */
static volatile uint16_t thiszctime          = 0;
static volatile uint16_t lastzctime          = 0;
static volatile uint16_t waitTime            = 0;
static volatile uint8_t  filter_level        = MAX_FILTER_LEVEL;

static volatile uint8_t  bemf_good_count     = 0;
static volatile uint8_t  bemf_timeout_count  = 0;
static volatile uint16_t zc_timeout_counter  = 0;
static volatile bool     desync_flag         = false;

/* ------------------------------------------------------------------ */
/* Commutation table                                                   */
/*   Each step: which phase is PWM-high, which is low, which floats,   */
/*   and the expected BEMF edge on the floating phase (forward dir).   */
/* ------------------------------------------------------------------ */
typedef struct {
    phase_t hi;
    phase_t lo;
    phase_t flt;
    bool    rising;   /* expected comparator edge in forward direction */
} com_step_t;

static const com_step_t com_table[7] = {
    { PHASE_A, PHASE_A, PHASE_A, false },   /* [0] unused                 */
    { PHASE_A, PHASE_B, PHASE_C, false },   /* 1: A+ B-  float C, falling */
    { PHASE_A, PHASE_C, PHASE_B, true  },   /* 2: A+ C-  float B, rising  */
    { PHASE_B, PHASE_C, PHASE_A, false },   /* 3: B+ C-  float A, falling */
    { PHASE_B, PHASE_A, PHASE_C, true  },   /* 4: B+ A-  float C, rising  */
    { PHASE_C, PHASE_A, PHASE_B, false },   /* 5: C+ A-  float B, falling */
    { PHASE_C, PHASE_B, PHASE_A, true  },   /* 6: C+ B-  float A, rising  */
};
/* NOTE: edge polarity depends on comparator wiring (+/- inputs) and   */
/* motor wiring. Verify on a scope and invert the column if needed.    */

/* ------------------------------------------------------------------ */
/* Phase output + comparator routing for the current step              */
/* ------------------------------------------------------------------ */
static inline void apply_step(uint8_t s)
{
    const com_step_t *c = &com_table[s];

    /* Float first to avoid momentary shoot-through across a leg */
    hal_phase_float(c->flt);
    hal_phase_low(c->lo);
    hal_phase_pwm(c->hi);
}

static inline void change_comp_input(void)
{
    const com_step_t *c = &com_table[step];
    hal_comp_select_input(c->flt);

    /* Reverse rotation flips the expected BEMF slope */
    zc_expected_rising = esc_dir_reversed ? !c->rising : c->rising;
}

/* ------------------------------------------------------------------ */
/* Commutate: advance the state machine one 60-deg electrical step     */
/* ------------------------------------------------------------------ */
static void commutate(void)
{
    if (!esc_dir_reversed) {
        step = (step >= 6) ? 1 : (uint8_t)(step + 1);
    } else {
        step = (step <= 1) ? 6 : (uint8_t)(step - 1);
    }

    apply_step(step);
    change_comp_input();

    /* Reset per-step detection state */
    zc_timeout_counter = 0;

    if (!polling_mode) {
        hal_comp_exti_clear();
        /* EXTI is re-armed after the demag blanking period in com_timer
           path — here we arm immediately but rely on the filter below. */
        hal_comp_exti_enable(zc_expected_rising);
    }
}

/* ------------------------------------------------------------------ */
/* Adaptive filter: more samples at low speed, fewer at high speed     */
/* ------------------------------------------------------------------ */
static inline void update_filter_level(void)
{
    if (commutation_interval > 6000)       filter_level = MAX_FILTER_LEVEL