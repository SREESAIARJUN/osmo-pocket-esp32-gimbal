# AM32-Style 6-Step Commutation & BEMF Zero-Crossing Detection

I don't know what "Cycle 31" refers to. It may be an iteration label on your side, or a specific firmware revision. If it points to a particular AM32 release or change, tell me what changed and I'll adjust.

The code below follows AM32's architecture:

- A six-step commutation table.
- Comparator-interrupt zero-cross detection at speed, with polled detection during startup and low RPM.
- A commutation timer that inserts the 30° delay minus timing advance.
- Filtering, desync detection and bidirectional support.

Hardware access sits behind a small HAL layer, so it ports to STM32F051, G071, AT32F415 and similar parts. For a 4-in-1 board, each ESC MCU runs one instance.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Phase drive modes ---------- */
typedef enum {
    PHASE_FLOAT = 0,   /* both FETs off, BEMF sense               */
    PHASE_PWM,         /* high-side PWM (complementary if enabled) */
    PHASE_LOW          /* low-side on (sink)                       */
} phase_mode_t;

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

/* ---------- Tunables (match AM32 eeprom semantics) ---------- */
#define MOTOR_POLES_DEFAULT          14
#define TIMING_ADVANCE_DEFAULT       2      /* 0..3 -> 0,7.5,15,22.5 deg   */
#define MIN_BEMF_COUNTS_UP           6      /* good ZCs before "running"    */
#define MIN_BEMF_COUNTS_DOWN         3
#define POLLING_MODE_THRESHOLD_US    2000   /* commutation interval above
                                               this -> poll comparator      */
#define POLLING_MODE_EXIT_US         1500
#define MAX_COMMUTATION_INTERVAL_US  20000  /* desync / stall threshold     */
#define FILTER_LEVEL_MAX             12
#define FILTER_LEVEL_MIN             2

/* ---------- Public API ---------- */
void  commutation_init(void);
void  comStep(uint8_t step);
void  changeCompInput(void);
void  commutate(void);
void  zcfoundroutine(void);
void  interruptRoutine(void);          /* comparator EXTI ISR body          */
void  PeriodElapsedCallback(void);     /* commutation timer ISR body        */
void  pollBemf(void);                  /* call from fast loop @ low speed   */
void  startMotor(void);
void  stopMotor(void);
void  setDirection(bool reversed);

/* ---------- State (exported for main loop / telemetry) ---------- */
extern volatile uint8_t  step;
extern volatile bool     running;
extern volatile bool     old_routine;           /* true = polling mode   */
extern volatile uint32_t commutation_interval;  /* us, smoothed           */
extern volatile uint32_t e_rpm;                 /* electrical RPM / 100   */
extern volatile uint16_t zero_crosses;
extern volatile uint8_t  bemf_timeout_happened;

#endif
```

---

## `hal_esc.h`: hardware abstraction (implement per MCU)

```c
#ifndef HAL_ESC_H
#define HAL_ESC_H
#include <stdint.h>
#include <stdbool.h>
#include "commutation.h"

/* Gate drive */
void hal_setPhase(phase_t ph, phase_mode_t mode);

/* Comparator: select which floating phase feeds COMP+ (COMP- = virtual neutral) */
void hal_compSelectPhase(phase_t ph);
bool hal_compOutput(void);              /* raw comparator output level */
void hal_compIrqEnable(bool rising);    /* arm EXTI on chosen edge     */
void hal_compIrqDisable(void);

/* Interval timer: free-running 1 MHz (1 tick = 1 us), 16-bit */
uint16_t hal_intervalTimerGet(void);
void     hal_intervalTimerReset(void);

/* Commutation timer: one-shot, fires PeriodElapsedCallback */
void hal_comTimerStart(uint16_t delay_us);
void hal_comTimerStop(void);

#endif
```

---

## `commutation.c`

```c
#include "commutation.h"
#include "hal_esc.h"

/* ==================== State ==================== */
volatile uint8_t  step                 = 1;
volatile bool     running              = false;
volatile bool     old_routine          = true;
volatile uint32_t commutation_interval = 12500;
volatile uint32_t e_rpm                = 0;
volatile uint16_t zero_crosses         = 0;
volatile uint8_t  bemf_timeout_happened = 0;

static volatile bool     forward          = true;
static volatile bool     rising           = true;   /* expected BEMF edge */
static volatile uint32_t thiszctime       = 0;
static volatile uint32_t lastzctime       = 0;
static volatile uint32_t commutation_intervals[6];
static volatile uint32_t waitTime         = 0;
static volatile uint32_t advance          = 0;
static volatile uint8_t  filter_level     = FILTER_LEVEL_MAX;
static volatile uint8_t  bad_count        = 0;
static volatile uint8_t  bemfcounter      = 0;
static volatile uint8_t  min_bemf_counts_up   = MIN_BEMF_COUNTS_UP;
static volatile uint8_t  min_bemf_counts_down = MIN_BEMF_COUNTS_DOWN;
static volatile uint8_t  timing_advance   = TIMING_ADVANCE_DEFAULT;
static volatile bool     zcfound          = false;

/* ==================== Commutation table ====================
 * Each step: phase driven with PWM, phase pulled low, floating phase.
 * BEMF on the floating phase alternates falling/rising each step.
 *
 *  step | PWM | LOW | FLOAT | BEMF edge (forward)
 *  -----+-----+-----+-------+--------------------
 *   1   |  A  |  B  |   C   | falling
 *   2   |  C  |  B  |   A   | rising
 *   3   |  C  |  A  |   B   | falling
 *   4   |  B  |  A  |   C   | rising
 *   5   |  B  |  C  |   A   | falling
 *   6   |  A  |  C  |   B   | rising
 */
typedef struct {
    phase_t pwm;
    phase_t low;
    phase_t flt;
    bool    rising_fwd;
} comm_step_t;

static const comm_step_t comm_table[7] = {
    [0] = {PHASE_A, PHASE_B, PHASE_C, false}, /* unused */
    [1] = {PHASE_A, PHASE_B, PHASE_C, false},
    [2] = {PHASE_C, PHASE_B, PHASE_A, true },
    [3] = {PHASE_C, PHASE_A, PHASE_B, false},
    [4] = {PHASE_B, PHASE_A, PHASE_C, true },
    [5] = {PHASE_B, PHASE_C, PHASE_A, false},
    [6] = {PHASE_A, PHASE_C, PHASE_B, true },
};

/* ==================== Phase drive ==================== */
void comStep(uint8_t s)
{
    if (s < 1 || s > 6) return;
    const comm_step_t *c = &comm_table[s];

    /* Float first to avoid shoot-through across the transition */
    hal_setPhase(c->flt, PHASE_FLOAT);
    hal_setPhase(c->low, PHASE_LOW);
    hal_setPhase(c->pwm, PHASE_PWM);
}

/* ==================== Comparator routing ==================== */
void changeCompInput(void)
{
    const comm_step_t *c = &comm_table[step];
    hal_compSelectPhase(c->flt);

    /* Direction reversal flips expected BEMF slope */
    rising = forward ? c->rising_fwd : !c->rising_fwd;
}

/* ==================== Commutate ==================== */
void commutate(void)
{
    if (forward) {
        step = (step >= 6) ? 1 : step + 1;
    } else {
        step = (step <= 1) ? 6 : step - 1;
    }

    /* Track per-step intervals for smoothing / RPM */
    commutation_intervals[step - 1] = commutation_interval;

    comStep(step);
    changeCompInput();

    if (!running) {
        /* open-loop / startup: keep counting ZCs from scratch */
        zero_crosses = 0;
    }
    bemfcounter = 0;
    zcfound     = false;
}

/* ==================== Timing / ZC found ==================== */
static inline void updateTiming(void)
{
    /* Commutation interval = time between successive ZCs (us).
     * Weighted average: (3*old + new)/4 is AM32-like and resists jitter. */
    uint32_t interval = thiszctime;
    if (interval == 0) return;

    commutation_interval = (3 * commutation_interval + interval) >> 2;

    /* Timing advance in 7.5 deg units of a 60 deg step:
     * 30 deg nominal delay minus advance.
     * interval covers 60 deg -> 7.5 deg = interval/8 */
    advance  = (commutation_interval >> 3) * timing_advance;
    waitTime = (commutation_interval >> 1) - advance;   /* 30 deg - adv */

    /* e_rpm (x100) : 60 steps... 6 steps per e-rev, interval in us */
    e_rpm = (commutation_interval > 0)
          ? (600000u / commutation_interval)  /* = 60e6 / (6*interval) / 100 */
          : 0;

    /* Adaptive comparator filter: fewer samples at high speed */
    if (commutation_interval < 100)       filter_level = FILTER_LEVEL_MIN;
    else if (commutation_interval < 400)  filter_level = 4;
    else if (commutation_interval < 1000) filter_level = 7;
    else                                  filter_level = FILTER_LEVEL_MAX;

    /* Mode switch with hysteresis */
    