# AM32-Style Six-Step Commutation and BEMF Zero-Crossing Detection

I couldn't tell what "Cycle 11" refers to, so I treated it as an iteration label. If it means a specific AM32 revision or a step in a series, tell me and I'll adjust.

This is a clean-room C implementation of the sensorless commutation core in the style of AM32. It targets STM32G0/F0/F4/AT32-class MCUs with an internal comparator. All register access goes through a thin HAL layer, so you can port it to a specific target.

It uses a per-motor context struct. On a typical 4-in-1 board each motor has its own MCU and runs one instance. On a single-MCU 4-in-1 you can run four instances.

---

## `bemf_commutation.h`

```c
#ifndef BEMF_COMMUTATION_H
#define BEMF_COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------------- Configuration ---------------- */
#define TIMER_TICK_HZ             2000000u   /* commutation timer: 0.5 us/tick   */
#define POLLING_MODE_INTERVAL     2000u      /* above this interval -> poll BEMF  */
#define INTERRUPT_MODE_INTERVAL   1600u      /* below this -> comparator IRQ (hysteresis) */
#define MIN_COMMUTATION_INTERVAL  40u        /* hard floor (ticks)                */
#define MAX_COMMUTATION_INTERVAL  20000u     /* below this speed, considered stalled */
#define DESYNC_RATIO_NUM          2u         /* new interval < old/2 -> desync    */
#define DEFAULT_ADVANCE_LEVEL     2u         /* 0..4 -> 0, 7.5, 15, 22.5, 30 deg  */
#define ZC_FILTER_MIN             2u
#define ZC_FILTER_MAX             12u
#define DEMAG_BLANK_DIVISOR       8u         /* blank 1/8 of interval after commutation */

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;
typedef enum { DRIVE_FLOAT = 0, DRIVE_LOW, DRIVE_PWM } phase_drive_t;

typedef struct {
    /* --- state machine --- */
    volatile uint8_t  step;              /* 1..6 */
    volatile bool     rising;            /* expected BEMF edge on floating phase */
    bool              forward;           /* direction */
    volatile bool     running;
    volatile bool     old_routine;       /* true