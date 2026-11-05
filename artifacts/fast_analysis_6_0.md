# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection

Below is an original, from-scratch implementation that follows the AM32 architecture. In a typical 4-in-1 board each motor has its own MCU, so this code runs per channel. It targets STM32F051/G071-class parts (COMP + EXTI + general-purpose timers). The hardware access is wrapped in a thin HAL layer, so you can map it to your board's pinout.

> **Note:** "Cycle 6" here means the 6-step trapezoidal commutation cycle (60° electrical per step, 360° per cycle).

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Tunables ---------------- */
#define TIMER_TICK_HZ           1000000UL   /* interval timer: 1 MHz -> 1 us/tick */
#define MIN_COMMUTATION_US      20          /* hard floor on 60deg interval       */
#define MAX_COMMUTATION_US      20000       /* below this speed -> desync/stall    */
#define POLLING_THRESHOLD_US    2000        /* above this interval use polling     */
#define DEFAULT_ADVANCE_LEVEL   2           /* in 7.5deg units: 2 = 15deg          */
#define MAX_FILTER_LEVEL        12
#define MIN_FILTER_LEVEL        2
#define BAD_ZC_LIMIT            12          /* consecutive bad ZCs -> desync       */

typedef enum { PH_A = 0, PH_B = 1, PH_C = 2 } phase_t;

typedef enum {
    PHASE_FLOAT = 0,   /* both FETs off - BEMF sense      */
    PHASE_LOW,         /* low-side FET on (sink)          */
    PHASE_PWM          /* high-side PWM (comp. low side)  */
} phase_mode_t;

typedef struct {
    phase_t pwm;        /* phase driven with PWM          */
    phase_t low;        /* phase tied to ground           */
    phase_t flt;        /* floating ph