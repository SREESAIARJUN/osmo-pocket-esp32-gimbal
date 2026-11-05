# AM32-Style 6-Step Commutation and BEMF Zero-Cross Detection (Cycle 25)

This is an original implementation in the style of AM32, targeting the STM32F051, the most common MCU on 4-in-1 boards. I treated "Cycle 25" as a revision tag and added the following:

- An adaptive comparator filter.
- Variable timing advance.
- Hybrid polling and interrupt zero-cross detection.
- Desync detection.

The hardware layer is isolated so you can port it to G071, L431 or AT32 targets.

## `commutation.h`

```c
#ifndef COMMUTATION_H
#define COMMUTATION_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Tunables ---------- */
#define POLLING_MODE_THRESHOLD   1500u   /* commutation interval (us) above which we poll   */
#define FILTER_LEVEL_MIN         2u      /* comparator samples that must agree (high rpm)   */
#define FILTER_LEVEL_MAX         12u     /* comparator samples that must agree (low rpm)    */
#define BLANKING_MIN_US          8u      /* demag/ringing blanking after commutation        */
#define ZC_TIMEOUT_US            20000u  /* no ZC within this -> desync / stall             */
#define DESYNC_RATIO_PCT         50u     /* interval jump > 50% considered bad zero cross   */
#define DESYNC_MAX_BAD           6u      /* bad ZCs in a row before forcing restart         */

typedef enum { PHASE_A = 0, PHASE_B, PHASE_C } phase_t;

typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_STARTING,      /* open-loop / polled */
    MOTOR_RUNNING,       /* closed-loop, interrupt ZC */
    MOTOR_DESYNC
} motor_state_t;

typedef struct {
    volatile uint8_t       step;                /* 1..6                                  */
    volatile motor_state_t state;
    volatile bool          forward;
    volatile bool          rising;              /* expected BEMF edge on floating phase  */

    volatile uint32_t      zc_time;             /* timestamp of last ZC (us)             */
    volatile uint32_t      commutation_interval;/* time between ZCs (us), = 60 elec deg  */
    volatile uint32_t      average_interval;    /* filtered interval                     */
    volatile uint32_t      waitTime;            /* ZC -> commutation delay               */
    volatile uint32_t      last_commutation_time;

    volatile uint8_t       filter_level;
    volatile uint8_t       advance_level;       /* 0..4  -> 0, 7.5, 15, 22.5, 30 deg      */
    volatile uint8_t       bad_count;
    volatile uint8_t       zc_found;
    volatile bool          comp_irq_enabled;
} motor_t;

extern motor_t motor;

void commutation_init(void);
void commutate(void);
void comp_zero_cross_irq(void);     /* call from ADC1_COMP_IRQHandler     */
void commutation_timer_irq(void);   /* call from TIM16 (one-shot) IRQ     */
void zero_cross_poll(void);         /* call from fast loop / TIM tick     */
void motor_start(void);
void motor_stop(void);

#endif
```

## `commutation.c`

```c
#include "commutation.h"
#include "stm32f0xx.h"

motor_t motor;

/* =====================================================================
 *  HARDWARE ABSTRACTION (STM32F051 reference mapping)
 *
 *  High side : TIM1 CH1/CH2/CH3 PWM  (PA8/PA9/PA10)
 *  Low side  : GPIO push-pull         (PB13/PB14/PB15)  - or TIM1 CHxN
 *  BEMF      : COMP1, non-inverting = virtual neutral (PA1)
 *              inverting mux       = phase A/B/C via PA4/PA5/PA0
 *  Timers    : TIM2  free-running 1 MHz  (timestamp, us)
 *              TIM16 one-shot             (commutation delay)
 * ===================================================================== */

#define TIMESTAMP()            (TIM2->CNT)

/* COMP1 INMSEL values: 100 = PA4 (DAC1/IO1), 101 = PA5, 110 = PA0 (IO1 alt) */
static const uint32_t comp_inm_sel[3] = {
    (4u << COMP_CSR_COMP1INSEL_Pos),   /* phase A on PA4 */
    (5u << COMP_CSR_COMP1INSEL_Pos),   /* phase B on PA5 */
    (6u << COMP_CSR_COMP1INSEL_Pos),   /* phase C on PA0 */
};

static GPIO_TypeDef * const low_port[3] = { GPIOB, GPIOB, GPIOB };
static const uint16_t       low_pin[3]  = { 1u << 13, 1u << 14, 1u << 15 };

static volatile uint32_t * const pwm_ccmr_reg[3] = {
    &TIM1->CCMR1, &TIM1->CCMR1, &TIM1->CCMR2
};
static const uint8_t pwm_ccmr_shift[3] = { 4, 12, 4 };   /* OCxM field position */

#define OCM_FORCED_INACTIVE  0x4u
#define OCM_PWM1             0x6u

static inline void phase_pwm(phase_t p)       /* high side switches with PWM */
{
    low_port[p]->BRR = low_pin[p];
    uint32_t r = *pwm_ccmr_reg[p];
    r &= ~(0x7u << pwm_ccmr_shift[p]);
    r |=  (OCM_PWM1 << pwm_ccmr_shift[p]);
    *pwm_ccmr_reg[p] = r;
}

static inline void phase_low(phase_t p)       /* low side fully on */
{
    uint32_t r = *pwm_ccmr_reg[p];
    r &= ~(0x7u << pwm_ccmr_shift[p]);
    r |=  (OCM_FORCED_INACTIVE << pwm_ccmr_shift[p]);
    *pwm_ccmr_reg[p] = r;
    low_port[p]->BSRR = low_pin[p];
}

static inline void phase_float(phase_t p)     /* both FETs off */
{
    uint32_t r = *pwm_ccmr_reg[p];
    r &= ~(0x7u << pwm_ccmr_shift[p]);
    r |=  (OCM_FORCED_INACTIVE << pwm_ccmr_shift[p]);
    *pwm_ccmr_reg[p] = r;
    low_port[p]->BRR = low_pin[p];
}

static inline void comp_select_phase(phase_t p)
{
    uint32_t csr = COMP->CSR;
    csr &= ~COMP_CSR_COMP1INSEL;
    csr |= comp_inm_sel[p];
    COMP->CSR = csr;
}

static inline bool comp_output(void)
{
    return (COMP->CSR & COMP_CSR_COMP1OUT) != 0u;
}

/* COMP1 -> EXTI line 21 */
static inline void comp_irq_enable(bool rising_edge)
{
    EXTI->PR = EXTI_PR_PR21;                       /* clear stale pending */
    if (rising_edge) { EXTI->RTSR |= EXTI_RTSR_TR21; EXTI->FTSR &= ~EXTI_FTSR_TR21; }
    else             { EXTI->FTSR |= EXTI_FTSR_TR21; EXTI->RTSR &= ~EXTI_RTSR_TR21; }
    EXTI->IMR |= EXTI_IMR_MR21;
    motor.comp_irq_enabled = true;
}

static inline void comp_irq_disable(void)
{
    EXTI->IMR &= ~EXTI_IMR_MR21;
    motor.comp_irq_enabled = false;
}

static inline void comm_timer_schedule(uint32_t us)
{
    TIM16->CNT = 0;
    TIM16->ARR = (us > 2u) ? us : 2u;
    TIM16->SR  = 0;
    TIM16->CR1 |= TIM_CR1_CEN;                    /* OPM set at init */
}

/* =====================================================================
 *  COMMUT