# AM32-Style ESC: Commutation State Machine and BEMF Zero-Crossing Detection (Cycle 1)

This is an original implementation in the style of AM32 (it is not copied from the GPL source). It targets STM32G0/F0/F4-class MCUs.

In a 4-in-1 ESC, each MCU usually runs one motor. So this code is written per motor instance, and all hardware access goes through a small HAL layer. You can bind that layer to TIM1, COMP1/2, and EXTI on your target.

## Contents

- `esc_commutation.h`: types, configuration, and the HAL contract.
- `esc_commutation.c`: the state machine, BEMF detection, and ISRs.
- `hal_stm32g071_example.c`: example HAL binding (partial).
- Integration notes, and a list of items deferred to Cycle 2.

---

## `esc_commutation.h`

```c
#ifndef ESC_COMMUTATION_H
#define ESC_COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Configuration ---------------- */
#define INTERVAL_TIMER_HZ        2000000u   /* 0.5 us per tick                  */
#define ZC_FILTER_MIN            2u         /* min consecutive comparator hits  */
#define ZC_FILTER_MAX            12u        /* used at low RPM                  */
#define POLLING_MODE_THRESHOLD   2000u      /* interval ticks: above -> poll    */
#define BLANKING_PERCENT         25u        /* % of interval ignored after comm */
#define MIN_COMMUTATION_TICKS    40u        /* ~20 us hard ceiling on eRPM      */
#define ZC_TIMEOUT_TICKS         60000u     /* 30 ms with no ZC -> desync       */
#define DESYNC_RATIO_NUM         2u         /* interval jump > 2x avg -> desync */
#define BEMF_GOOD_COUNT_RUNNING  12u        /* ZCs needed before "running"      */
#define TIMING_ADVANCE_DEG_x8    (15u * 8u / 7u) /* ~15 deg in 1/8-step units   */

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;
typedef enum { DRIVE_FLOAT = 0, DRIVE_LOW, DRIVE_PWM } drive_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,     /* blind/kick commutation, waiting for good BEMF */
    MOTOR_RUNNING,      /* closed-loop on zero crossings                */
    MOTOR_DESYNC        /* lost sync, will restart                      */
} motor_state_t;

typedef struct {
    /* state machine */
    volatile motor_state_t state;
    volatile uint8_t  step;               /* 0..5                          */
    bool              reversed;

    /* timing (interval-timer ticks) */
    volatile uint16_t this_zc_time;
    volatile uint16_t avg_interval;       /* filtered 60-electrical-degree */
    volatile uint16_t commutation_delay;
    volatile uint16_t blanking_ticks;

    /* zero-cross detection */
    volatile bool     zc_armed;           /* past blanking, looking for ZC */
    volatile bool     zc_expect_rising;
    volatile uint8_t  zc_filter_level;
    volatile uint8_t  zc_filter_count;
    volatile bool     polling_mode;
    volatile uint16_t good_zc_count;
    volatile uint16_t zc_timeout_count;

    /* stats */
    volatile uint32_t desync_events;
    uint8_t           advance_x8;         /* advance in 1/8 of a 60deg step */
} esc_motor_t;

/* ---------------- HAL contract (implement per target) ---------------- */
void     hal_set_phase(phase_t ph, drive_t mode);  /* complementary PWM/low/hi-Z */
void     hal_comp_select(phase_t floating_ph);     /* mux comparator input       */
bool     hal_comp_output(void);                    /* true: phase > neutral      */
void     hal_comp_irq_enable(bool rising_edge);
void     hal_comp_irq_disable(void);
uint16_t hal_interval_timer_read(void);
void     hal_interval_timer_reset(void);
void     hal_com_timer_schedule(uint16_t ticks);   /* one-shot -> esc_com_timer_isr */
void     hal_com_timer_cancel(void);
void     hal_all_phases_float(void);

/* ---------------- API ---------------- */
void esc_init(esc_motor_t *m, bool reversed);
void esc_start(esc_motor_t *m);
void esc_stop(esc_motor_t *m);

/* ISR entry points */
void esc_com_timer_isr(esc_motor_t *m);    /* commutation one-shot expired     */
void esc_comparator_isr(esc_motor_t *m);   /* comparator edge (EXTI)           */
void esc_pwm_tick_isr(esc_motor_t *m);     /* every PWM period: polling + timeouts */

#endif
```

---

## `esc_commutation.c`

```c
#include "esc_commutation.h"

/*
 * Six-step table, forward rotation.
 *   step  high(PWM)  low   float  BEMF edge on floating phase
 *    0      A        B      C      falling
 *    1      A        C      B      rising
 *    2      B        C      A      falling
 *    3      B        A      C      rising
 *    4      C        A      B      falling
 *    5      C        B      A      rising
 * Reverse rotation walks the table backwards, which inverts the edge polarity.
 */
typedef struct {
    drive_t a, b, c;
    phase_t floating;
    bool    rising;   /* forward direction */
} comm_step_t;

static const comm_step_t COMM_TABLE[6] = {
    { DRIVE_PWM,   DRIVE_LOW,   DRIVE_FLOAT, PHASE_C, false },
    { DRIVE_PWM,   DRIVE_FLOAT, DRIVE_LOW,   PHASE_B, true  },
    { DRIVE_FLOAT, DRIVE_PWM,   DRIVE_LOW,   PHASE_A, false },
    { DRIVE_LOW,   DRIVE_PWM,   DRIVE_FLOAT, PHASE_C, true  },
    { DRIVE_LOW,   DRIVE_FLOAT, DRIVE_PWM,   PHASE_B, false },
    { DRIVE_FLOAT, DRIVE_LOW,   DRIVE_PWM,   PHASE_A, true  },
};

/* ---------------------------------------------------------------- */
/* Phase output                                                      */
/* ---------------------------------------------------------------- */

static void apply_step(esc_motor_t *m)
{
    const comm_step_t *s = &COMM_TABLE[m->step];

    /* Float first so a phase is never briefly driven both ways
       (dead time is handled in TIM1 BDTR, but be conservative). */
    if (s->a == DRIVE_FLOAT) hal_set_phase(PHASE_A, DRIVE_FLOAT);
    if (s->b == DRIVE_FLOAT) hal_set_phase(PHASE_B, DRIVE_FLOAT);
    if (s->c == DRIVE_FLOAT