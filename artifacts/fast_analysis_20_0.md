# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection (C)

I'm treating "Cycle 20" as your iteration label. I don't have earlier cycles, so this is a complete, self-contained implementation. It follows AM32's architecture: comparator interrupt, filtered zero-cross, commutation timer with timing advance, and a polled fallback at low speed. Target is STM32F0/G0-class with LL drivers.

**Per-motor instancing:** On a 4-in-1 board each ESC runs its own MCU and its own copy of this code, so there is one motor per MCU. Hardware hooks are isolated in the HAL section so you can port to AT32 or GD32.

---

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define INTERVAL_TIMER_HZ        2000000UL  /* 0.5 us per tick                   */
#define ZC_FILTER_MAX            12         /* comparator re-reads at low speed */
#define ZC_FILTER_MIN            2          /* comparator re-reads at high speed*/
#define POLLING_MODE_THRESHOLD   2000       /* interval (ticks) above => poll   */
#define POLLING_MODE_HYST        1500       /* interval below => back to IRQ   */
#define BEMF_GOOD_COUNTS_RUN     6          /* consecutive ZCs before "running" */
#define MAX_COMMUTATION_INTERVAL 60000      /* ticks: slower than this = stall */
#define MIN_COMMUTATION_INTERVAL 40         /* ticks: guard against noise      */
#define ZC_TIMEOUT_TICKS         (MAX_COMMUTATION_INTERVAL * 2)

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;
typedef enum { DRIVE_FLOAT = 0, DRIVE_LOW, DRIVE_PWM } drive_t;

typedef struct {
    phase_t pwm_phase;      /* high-side PWM (complementary)          */
    phase_t low_phase;      /* low-side held on                        */
    phase_t float_phase;    /* undriven -> BEMF sensed here            */
    bool    rising;         /* expected BEMF ed