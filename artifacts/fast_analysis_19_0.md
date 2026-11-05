# AM32-Style 6-Step Commutation & BEMF Zero-Crossing Detection

I wasn't sure what "Cycle 19" refers to. It might be an internal iteration or milestone label in your project. If it refers to a specific AM32 release or a known bug-fix cycle, tell me and I'll align the code to it.

Below is a clean-room implementation that follows the AM32 architecture. It targets an STM32G071/F051-class MCU with:

- **TIM1**: complementary 3-phase PWM
- **COMP2**: BEMF sensing against a virtual neutral
- **TIM14**: commutation scheduling
- **TIM2**: free-running interval timer, 1 µs tick

For a 4-in-1 board, each ESC MCU runs one instance of this code. The `esc_motor_t` struct also lets you instance it per motor on a multi-motor MCU.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Tunables ---------------- */
#define COMM_STEPS              6
#define ZC_FILTER_MIN           2       /* min consecutive comparator agreements   */
#define ZC_FILTER_MAX           12      /* max filter at low speed                 */
#define ZC_POLLING_THRESHOLD_US 2000    /* below this e-period/6 -> use interrupts */
#define BLANKING_MIN_US         4       /* demag/ringing blank after commutation    */
#define DESYNC_MAX_RATIO_NUM    3       /* interval change > 3/2 -> suspect desync */
#define DESYNC_MAX_RATIO_DEN    2
#define DESYNC_ERR_LIMIT        8
#define STARTUP_INTERVAL_US     12000
#define MAX_INTERVAL_US         65000   /* stall timeout                            */
#define ADVANCE_LEVEL_DEFAULT   2       /* 0..4, each = 7.5 electrical degrees      */

typedef enum {
    PHASE_A = 0,
    PHASE_B,
    PHASE_C
} phase_t;

typedef enum {
    DRIVE_FLOAT = 0,   /* both FETs off, ph