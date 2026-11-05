# AM32-Style Commutation State Machine and BEMF Zero-Crossing Detection

This is a self-contained C implementation modeled on the AM32 architecture. On a 4-in-1 ESC, each motor has its own MCU running one instance of this firmware, so the code below drives a single motor. The hardware access is isolated behind a small HAL layer. Bind those functions to your target (STM32F051, G071, AT32F421, etc.).

I wasn't sure what "Cycle 9" refers to, so I treated it as an iteration label. If it refers to a specific spec revision, let me know and I'll align the code to it.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define COMM_STEPS                 6
#define ZC_FILTER_MAX              12     /* max comparator samples (low RPM)    */
#define ZC_FILTER_MIN              2      /* min comparator samples (high RPM)   */
#define INTERRUPT_MODE_INTERVAL    1000   /* below this interval -> IRQ mode (us-ish ticks) */
#define POLLING_MODE_INTERVAL      1400   /* above this -> polling mode (hysteresis) */
#define BEMF_TIMEOUT_LIMIT         10     /* missed ZCs before desync declared   */
#define MIN_COMM_INTERVAL          40     /* hard RPM ceiling guard (timer ticks)*/
#define MAX_COMM_INTERVAL          20000  /* below this RPM we consider stalled  */
#define DEFAULT_ADVANCE_LEVEL      2      /* 0..4 -> 0, 7.5, 15, 22.5, 30 deg     */

/* ---------- Phase identifiers ---------- */
typedef enum { PHASE_A = 0, PHASE_B = 1, PHASE_C = 2 } phase_t;

/* ---------- Detection mode ---------- */
typedef enum { ZC_MODE_POLLING = 0, ZC_MODE_INTERRUPT = 1 } zc_mode_t;

/* ---------- Motor run state ---------- */
typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,       /* open-loop kicks until BEMF locks */
    MOTOR_RUNNING,        /* closed-loop sensorless */
    MOTOR_DESYNC          /* lost sync, needs restart */
} motor_state_t;

typed