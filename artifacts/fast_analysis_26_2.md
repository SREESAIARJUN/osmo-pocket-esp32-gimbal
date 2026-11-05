# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection (Cycle 26)

This is an original implementation that follows the AM32 architecture. It is not a verbatim copy of the GPLv3 AM32 source. It targets an STM32F051/G071-class MCU that drives one motor channel, so a 4-in-1 board runs one instance per MCU.

## Hardware assumptions

- TIM1 provides complementary PWM on CH1/CH2/CH3, which map to phases A/B/C.
- COMP1 is the internal comparator. Its inverting input is muxed to the floating phase, and its non-inverting input is tied to the virtual neutral.
- TIM2 is a free-running 1 MHz timer used for commutation interval measurement.
- TIM14 is a one-shot timer that schedules the delayed commutation after a zero-cross.
- EXTI line 21 is mapped to the COMP1 output.

You need to supply the register-level functions in the HAL abstraction section for your board.

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_ALIGN,
    MOTOR_OPEN_LOOP,      /* forced commutation / polling ZC */
    MOTOR_CLOSED_LOOP,    /* interrupt-driven ZC */
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    volatile motor_state_t state;
    volatile uint8_t  step;               /* 1..6 */
    volatile uint8_t  rising;             /* expected BEMF edge on floating phase */
    volatile uint8_t  direction;          /* 0 = forward, 1 = reverse */

    volatile uint32_t commutation_interval;   /* us, filtered */
    volatile uint32_t commutation_intervals[6];
    volatile uint32_t last_zc_time;
    volatile uint32_t this_zc_time;
    volatile uint16_t zc_count;           /* good ZCs since start */
    volatile uint16_t bad_count;          /* missed / desync counter */

    volatile uint16_t advance_level;      /* 0..4 -> 0..30 deg in 7.5 deg steps */
    volatile uint32_t advance_us;
    volatile uint32_t waitTime;

    volatile uint8_t  filter_level;       /* consecutive comparator samples */
    volatile uint16_t blanking_us;        /* demag / ringing blanking window */
    volatile bool     zc_found;
    volatile bool     old_routine;        /* true = polling mode */

    volatile uint16_t duty;               /* 0..TIM1_ARR */
} motor_t;

extern motor_t motor;

void commutation_init(void);
void commutate(void);
void comStep(uint8_t step);
void zc_found_routine(void);
void poll_zero_crossing(void);
void interrupt_zero_crossing(void);   /* call from COMP / EXTI ISR */
void commutation_timer_isr(void);     /* call from TIM14 ISR */
void motor_tick_1khz(void);           /* desync watchdog, call at 1 kHz */

#endif
```

## `commutation.c`

```c
#include "commutation.h"

/* ---------------- HAL abstraction (implement per target) ---------------- */
extern void     phaseA_PWM(void);  extern void phaseA_LOW(void);  extern void phaseA_FLOAT(void);
extern void     phaseB_PWM(void);  extern void phaseB_LOW(void);  extern void phaseB_FLOAT(void);
extern void     phaseC_PWM(void);  extern void phaseC_LOW(void);  extern void phaseC_FLOAT(void);
extern void     comp_select_phase(uint8_t phase);   /* 0=A 1=B 2=C */
extern uint8_t  comp_output(void);                  /* raw COMP1 output bit */
extern void     comp_exti_enable(bool rising_edge);
extern void     comp_exti_disable(void);
extern uint32_t interval_timer_get(void);           /* TIM2 CNT, 1 us */
extern void     interval_timer_reset(void);
extern void     com_timer_set_oneshot(uint32_t us); /* TIM14 */
extern void     com_timer_disable(void);
extern void     all_phases_float(void);

#define PHASE_A 0
#define PHASE_B 1
#define PHASE_C 2

#define POLLING_MODE_THRESHOLD_US   2000u  /* below this interval -> interrupt mode */
#define POLLING_MODE_EXIT_US        2500u  /* hysteresis */
#define MIN_INTERVAL_US             40u
#define MAX_INTERVAL_US             20000u
#define DESYNC_BAD_LIMIT            12u
#define CLOSED_LOOP_ZC_COUNT        30u

motor_t motor;

/* ---------------- 6-step commutation table ----------------------------
 * step : high (PWM) | low | float | BEMF on float
 *  1   :     A      |  B  |   C   | rising
 *  2   :     C      |  B  |   A   | falling
 *  3   :     C      |  A  |   B   | rising
 *  4   :     B      |  A  |   C   | falling
 *  5   :     B      |  C  |   A   | rising
 *  6   :     A      |  C  |   B   | falling
 * -------------------------------------------------------------------- */
void comStep(uint8_t step)
{
    switch (step) {
    case 1: phaseC_FLOAT(); phaseB_LOW();  phaseA_PWM(); break;
    case 2: phaseA_FLOAT(); phaseB_LOW();  phaseC_PWM(); break;
    case 3: phaseB_FLOAT(); phaseA_LOW();  phaseC_PWM(); break;
    case 4: phaseC_FLOAT(); phaseA_LOW();  phaseB_PWM(); break;
    case 5: phaseA_FLOAT(); phaseC_LOW();  phaseB_PWM(); break;
    case 6: phaseB_FLOAT(); phaseC_LOW();  phaseA_PWM(); break;
    default: all_phases_float(); break;
    }
}

/* Route comparator to the floating phase and set expected edge polarity */
static void changeCompInput(void)
{
    static const uint8_t float_phase[7] = { 0, PHASE_C, PHASE_A, PHASE_B,
                                            PHASE_C, PHASE_A, PHASE_B };
    comp_select_phase(float_phase[motor.step]);

    /* Odd steps see a rising BEMF; reversing direction inverts polarity */
    motor.rising = (motor.step & 1u) ? 1u : 0u;
    if (motor.direction) motor.rising ^= 1u;
}

void commutate(void)
{
    if (!motor.direction) {
        if (++motor.step > 6) motor.step = 1;
    } else {
        if (--motor.step < 1) motor.step = 6;
    }

    comStep(motor.step);
    changeCompInput();
    motor.zc_found = false;

    /* After a commutation the floating phase rings (demag). Blank it. */
    motor.blanking_us = (uint16_t)(motor.commutation_interval >> 3); /* ~7.5 deg */
    if (motor.blanking_us < 10) motor.blanking_us = 10;

    if (!motor.old_routine) {
        /* EXTI fires on the comparator output; the ISR sees the inverted logic */
        comp_exti_enable(!motor.rising);
    }
}

/* ---------------- ZC found: compute interval & schedule commutation ---- */
void zc_found_routine(void)
{
    motor.this_zc_time = interval_timer_get();
    interval_timer_reset();

    uint32_t interval = motor.this_zc_time;
    if (interval < MIN_INTERVAL_US) interval = MIN_INTERVAL_US;
    if (interval > MAX_INTERVAL_US) interval = MAX_INTERVAL_US;

    /* Average the last 6 steps to smooth out per-phase asymmetry */
    motor.commutation_intervals[motor.step - 1] = interval;
    uint32_t sum = 0;
    for (int i = 0; i < 6; i++) sum += motor.commutation_intervals[i];
    motor.commutation_interval = (motor.commutation_interval + sum / 6u) >> 1;

    /* Ideal commutation is 30 deg (interval/2) after ZC, minus advance.
       advance_level * 7.5 deg = advance_level * interval / 8          */
    motor.advance_us = (motor.commutation_interval * motor.advance_level) >> 3;
    uint32_t half = motor.commutation_interval >> 1;
    motor.waitTime = (half > motor.advance_us) ? (half - motor.advance_us) : 1u;

    if (motor.zc_count < 0xFFFF) motor.zc_count++;
    if (motor.bad_count) motor.bad_count--;

    if (motor.old_routine) {
        /* Polling mode: busy-wait commutation (low speed only) */
        uint32_t t0 = interval_timer_get();
        while ((interval_timer_get() - t0) < motor.waitTime) { }
        commutate();
    } else {
        com_timer_set_oneshot(motor.waitTime);
    }

    /* Mode switching with hysteresis */
    if (motor.zc_count > CLOSED_LOOP_ZC_COUNT) {
        if (motor.old_routine && motor.commutation_interval < POLLING_MODE_THRESHOLD_US) {
            motor.old_routine = false;
            motor.state = MOTOR_CLOSED_LOOP;
        } else if (!motor.old_routine && motor.commutation_interval > POLLING_MODE_EXIT_US) {
            motor.old_routine = true;
            comp_exti_disable();
            motor.state = MOTOR_OPEN_LOOP;
        }
    }
}

/* ---------------- Polling-mode ZC (startup / low RPM) ------------------ */
void poll_zero_crossing(void)
{
    if (motor.zc_found) return;
    if (interval_timer_get() < motor.blanking_us) return;   /* demag blank */

    uint8_t matches = 0;
    for (uint8_t i = 0; i < motor.filter_level; i++) {
        uint8_t out = comp_output();
        /* rising BEMF -> comparator output goes low (inverting input = phase) */
        if (( motor.rising && out == 0) ||
            (!motor.rising && out == 1)) {
            matches++;