# AM32-Style 4-in-1 ESC: Commutation State Machine & BEMF Zero-Cross Detection (Cycle 3)

I couldn't tell what "Cycle 3" refers to in your project, so I've treated it as the third iteration of the motor core. This iteration adds:

- **Interrupt-driven comparator zero-cross detection** with blanking and a digital glitch filter.
- **Polled zero-cross detection** for startup and low speed.
- **Timing advance**, **direction reversal**, and **desync/stall recovery**.

This is an original implementation that follows AM32's architecture. It is not copied from the AM32 GPL sources. The hardware layer is abstracted through a small set of `hal_*` hooks, so you can port it to an F051, G071, or AT32F421.

---

## `motor_commutation.h`

```c
#ifndef MOTOR_COMMUTATION_H
#define MOTOR_COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define COMP_FILTER_SAMPLES_MIN     2u     /* glitch filter at high eRPM      */
#define COMP_FILTER_SAMPLES_MAX     12u    /* glitch filter at low eRPM       */
#define POLLING_MODE_ENTER_INTERVAL 2000u  /* us  - slower than this => poll  */
#define POLLING_MODE_EXIT_INTERVAL  1600u  /* us  - hysteresis                */
#define DESYNC_MAX_INTERVAL         20000u /* us  - no ZC for this long = stall */
#define DESYNC_JUMP_RATIO_NUM       2u     /* interval may not halve in one step */
#define DESYNC_JUMP_RATIO_DEN       1u
#define BLANKING_MIN_US             8u
#define ZC_TIMEOUT_LIMIT            100u   /* consecutive misses before restart */

typedef enum {
    PHASE_FLOAT = 0,
    PHASE_PWM,      /* high side PWM, complementary low side (if comp_pwm) */
    PHASE_LOW       /* low side on solid                                     */
} phase_state_t;

typedef enum { PH_A = 0, PH_B, PH_C } phase_id_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,     /* open-loop / polled ZC          */
    MOTOR_RUNNING_POLL, /* closed-loop, polled comparator */
    MOTOR_RUNNING_INT   /* closed-loop, comparator I