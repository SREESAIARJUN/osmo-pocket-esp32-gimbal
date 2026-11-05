# AM32-Style ESC Commutation State Machine and BEMF Zero-Crossing Detection

This is a self-contained C implementation of the sensorless 6-step commutation core used by AM32-class ESCs. It covers:

- The commutation table
- Comparator-based zero-cross detection with filtering
- Timing advance
- Low-speed polling mode
- Desync/stall recovery

It is written for STM32-class MCUs (F051/G071/G431 style). In a 4-in-1 ESC, each motor normally has its own MCU running one instance of this.

> **Note:** I'm not sure what "Cycle 13" refers to in your project. I've treated it as a revision tag and put it in the header. If it refers to a specific spec or iteration requirement, tell me and I'll adapt the code.

---

## `commutation.h`

```c
/* commutation.h  - Sensorless BLDC commutation core (rev: Cycle 13) */
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Configuration ---------- */
#define TIMER_TICK_HZ            1000000UL  /* interval/com timers @ 1 MHz (1 us/tick)  */
#define POLLING_MODE_INTERVAL    2000U      /* > this interval (us) -> use polling ZC   */
#define POLLING_EXIT_INTERVAL    1500U      /* hysteresis to go back to interrupt mode   */
#define ZC_TIMEOUT_US            20000U     /* no ZC within this -> bemf timeout         */
#define BEMF_TIMEOUT_LIMIT       12U        /* consecutive timeouts before desync        */
#define MIN_FILTER_LEVEL         2U
#define MAX_FILTER_LEVEL         12U
#define MIN_BLANKING_US          8U         /* ignore comparator right after commutation */
#define DEFAULT_ADVANCE_LEVEL    2U         /* 0..4 -> 0, 7.5, 15, 22.5, 30 electrical deg */

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;
typedef enum { EDGE_FALLING = 0, EDGE_RISING = 1 } zc_edge_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTUP,       /* open-loop / forced commutation, polling ZC   */
    MOTOR_RUNNING_POLL,  /* closed loop, low speed, polled comparator     */
    MOTOR_RUNNING_INT,   /* closed loop, comparator interrupt driven      */
    MOTOR_DESYNC         /* lost sync, recovering                         */
} motor_state_t;

typedef struct {
    phase_t   high;      /* phase driven with PWM (high side) */
    phase_t   low;       /* phase held low (low side on)      */
    phase_t   floating;  /* phase sensed by comparator        */
    zc_edge_t edge;      /* expected BEMF edge (forward dir)  */
} comm_step_t;

typedef struct {
    volatile motor_state_t state;
    volatile uint8_t  step;               /* 1..6 */
    volatile bool     forward;
    volatile bool     zc_found;
    volatile uint16_t commutation_interval;     /* averaged, in us (60 elec deg) */
    volatile uint16_t last_zc_time;
    volatile uint16_t this_zc_time;
    volatile uint16_t advance;                  /* in timer ticks */
    volatile uint16_t wait_time;
    volatile uint8_t  advance_level;
    volatile uint8_t  filter_level;
    volatile uint8_t  bemf_timeout_count;
    volatile uint32_t zc_counter;               /* good zero crosses since start */
    volatile uint32_t erpm_period_us;           /* 6 * interval */
} commutation_t;

extern commutation_t g_com;

/* Public API */
void comm_init(void);
void comm_start(bool forward);
void comm_stop(void);
void comm_poll_task(void);          /* call from fast loop (e.g. 20 kHz) */
void comm_zc_comparator_isr(void);  /* COMP IRQ handler body            */
void comm_timer_isr(void);          /* commutation timer IRQ body       */
uint32_t comm_get_erpm(void);

/* ---------- Hardware abstraction (implement per target) ---------- */
void hal_phase_pwm(phase_t p);      /* complementary PWM on phase      */
void hal_phase_low(phase_t p);      /* low side on, high side off      */
void hal_phase_float(phase_t p);    /* both FETs off                   */
void hal_comp_select(phase_t p, zc_edge_t edge); /* mux + trigger edge  */
bool hal_comp_output(void);         /* raw comparator output level     */
void hal_comp_irq_enable(void);
void hal_comp_irq_disable(void);
uint16_t hal_interval_timer_read(void);   /* free-running 1 MHz        */
void hal_interval_timer_reset(void);
void hal_com_timer_schedule(uint16_t ticks); /* one-shot, fires comm_timer_isr */
void hal_com_timer_cancel(void);
void hal_all_phases_off(void);

#endif
```

---

## `commutation.c`

```c
/* commutation.c  - rev Cycle 13 */
#include "commutation.h"

commutation_t g_com;

/*
 * 6-step table (forward rotation).
 * The floating phase was driven in the previous step. Its BEMF therefore
 * moves away from that rail:
 *   previously HIGH -> falling edge
 *   previously LOW  -> rising edge
 * In reverse, the step order is inverted, so every expected edge flips.
 */
static const comm_step_t k_steps[7] = {
    { PHASE_A, PHASE_A, PHASE_A, EDGE_RISING  },   /* [0] unused */
    { PHASE_A, PHASE_B, PHASE_C, EDGE_FALLING },   /* 1: C was high (step 6) */
    { PHASE_A, PHASE_C, PHASE_B, EDGE_RISING  },   /* 2: B was low  (step 1) */
    { PHASE_B, PHASE_C, PHASE_A, EDGE_FALLING },   /* 3: A was high (step 2) */
    { PHASE_B, PHASE_A, PHASE_C, EDGE_RISING  },   /* 4: C was low  (step 3) */
    { PHASE_C, PHASE_A, PHASE_B, EDGE_FALLING },   /* 5: B was high (step 4) */
    { PHASE_C, PHASE_B, PHASE_A, EDGE_RISING  },   /* 6: A was low  (step 5) */
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */
static inline zc_edge_t expected_edge(uint8_t step)
{
    zc_edge_t e = k_steps[step].edge;
    return g_com.forward ? e : (zc_edge_t)!e;
}

/* Comparator output level that means "zero cross has happened". */
static inline bool zc_target_level(uint8_t step)
{
    return expected_edge(step) == EDGE_RISING;
}

static void apply_step(uint8_t step)
{
    const comm_step_t *s = &k_steps[step];
    /* Float first to avoid momentary shoot-through between phases. */
    hal_phase_float(s->floating);
    hal_phase_low(s->low);
    hal_phase_pwm(s->high);
    hal_comp_select(s->floating, expected_edge(step));
}

static void advance_step(void)
{
    if (g_com.forward) {
        g_com.step = (g_com.step >= 6) ? 1 : g_com.step + 1;
    } else {
        g_com.step