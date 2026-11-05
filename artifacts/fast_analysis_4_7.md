# AM32-Style 6-Step Commutation and BEMF Zero-Crossing Detection (Cycle 4)

Below is a self-contained C implementation modeled on the AM32 architecture. It targets an STM32F0/G0-class MCU with an internal comparator. The code is split into three files:

- a hardware abstraction header (`phase_hal.h`)
- the commutation and zero-crossing core (`commutation.c`)
- a public header (`commutation.h`)

For a 4-in-1 board, compile one instance per ESC MCU, since each motor has its own MCU on typical 4-in-1 designs.

---

## `phase_hal.h`: Hardware Abstraction

```c
#ifndef PHASE_HAL_H
#define PHASE_HAL_H

#include <stdint.h>
#include "stm32f0xx_ll_tim.h"
#include "stm32f0xx_ll_comp.h"
#include "stm32f0xx_ll_exti.h"
#include "stm32f0xx_ll_gpio.h"

/* ---------- Timers ----------
 * TIM1  : 3-phase complementary PWM
 * TIM2  : free-running 1 MHz interval timer (0.5us/tick if prescaled for 2MHz)
 * TIM14 : one-shot commutation timer (fires at zero-cross + advance delay)
 */
#define INTERVAL_TIMER          TIM2
#define COM_TIMER               TIM14
#define INTERVAL_TIMER_COUNT()  (INTERVAL_TIMER->CNT)
#define INTERVAL_TIMER_RESET()  (INTERVAL_TIMER->CNT = 0)
#define COM_TIMER_SET(t)        do { COM_TIMER->CNT = 0; COM_TIMER->ARR = (t); \
                                     COM_TIMER->SR = 0; } while (0)
#define COM_TIMER_ENABLE_IRQ()  (COM_TIMER->DIER |= TIM_DIER_UIE)
#define COM_TIMER_DISABLE_IRQ() (COM_TIMER->DIER &= ~TIM_DIER_UIE)

/* ---------- Comparator ---------- */
#define COMP_EXTI_LINE          LL_EXTI_LINE_21
#define COMP_OUTPUT()           (LL_COMP_ReadOutputLevel(COMP1))   /* 1 = high */
#define COMP_IRQ_ENABLE()       LL_EXTI_EnableIT_0_31(COMP_EXTI_LINE)
#define COMP_IRQ_DISABLE()      LL_EXTI_DisableIT_0_31(COMP_EXTI_LINE)
#define COMP_IRQ_CLEAR()        LL_EXTI_ClearFlag_0_31(COMP_EXTI_LINE)
#define COMP_TRIG_RISING()      do { LL_EXTI_DisableFallingTrig_0_31(COMP_EXTI_LINE); \
                                     LL_EXTI_EnableRisingTrig_0_31(COMP_EXTI_LINE); } while (0)
#define COMP_TRIG_FALLING()     do { LL_EXTI_DisableRisingTrig_0_31(COMP_EXTI_LINE); \
                                     LL_EXTI_EnableFallingTrig_0_31(COMP_EXTI_LINE); } while (0)

/* Comparator inverting-input mux: selects which floating phase is sensed.
 * Non-inverting input is tied to the virtual neutral (star resistor network). */
#define COMP_INPUT_PHASE_A   LL_COMP_INPUT_MINUS_IO1   /* board specific */
#define COMP_INPUT_PHASE_B   LL_COMP_INPUT_MINUS_DAC1_CH1
#define COMP_INPUT_PHASE_C   LL_COMP_INPUT_MINUS_DAC1_CH2
#define COMP_SELECT(in)      LL_COMP_SetInputMinus(COMP1, (in))

/* ---------- Phase drive primitives (implemented in phase_hal.c) ---------- */
void phaseA_PWM(void);   void phaseA_FLOAT(void);   void phaseA_LOW(void);
void phaseB_PWM(void);   void phaseB_FLOAT(void);   void phaseB_LOW(void);
void phaseC_PWM(void);   void phaseC_FLOAT(void);   void phaseC_LOW(void);
void allPhases_FLOAT(void);
void allPhases_LOW(void);          /* brake */

#endif
```

---

## `commutation.c`: Core State Machine and Zero-Crossing Detection

```c
#include <stdint.h>
#include <stdbool.h>
#include "phase_hal.h"
#include "commutation.h"

/* ===================== Tunables ===================== */
#define MIN_FILTER_LEVEL        2       /* comparator re-reads at high eRPM   */
#define MAX_FILTER_LEVEL        12      /* comparator re-reads at low eRPM    */
#define POLLING_MODE_THRESHOLD  1500    /* interval ticks; slower => poll ZC  */
#define STARTUP_BEMF_COUNT      12      /* good ZCs before closed loop        */
#define STALL_TIMEOUT_TICKS     45000   /* no ZC within this => desync/stall  */
#define MIN_COMM_INTERVAL       40      /* sanity floor (~max eRPM)           */
#define BLANKING_DIVISOR        4       /* ignore comp for interval/N after comm */

/* ===================== State ===================== */
volatile uint8_t  step = 1;                  /* 1..6 */
volatile bool     rising = true;             /* expected BEMF edge direction */
volatile bool     forward = true;
volatile bool     running = false;
volatile bool     old_routine = true;        /* polling ZC mode (low speed)  */
volatile bool     zcfound = false;

volatile uint32_t commutation_interval = 12500;
volatile uint32_t average_interval     = 12500;
volatile uint32_t this_interval        = 0;
volatile uint32_t last_interval        = 0;
volatile uint32_t commutation_intervals[6] = {0};
volatile uint32_t waitTime   = 0;
volatile uint32_t blankTime  = 0;
volatile uint16_t bemfcounter = 0;
volatile uint8_t  filter_level = MAX_FILTER_LEVEL;
volatile uint8_t  advance_level = 2;         /* 0..3 => 0,7.5,15,22.5 deg    */
volatile uint32_t advance = 0;
volatile uint16_t zero_crosses = 0;
volatile uint8_t  desync_flag = 0;

/* ===================== Phase step table =====================
 * Step | High(PWM) | Low | Float | Expected BEMF on float
 *   1  |    A      |  B  |   C   | falling (fwd)
 *   2  |    C      |  B  |   A   | rising
 *   3  |    C      |  A  |   B   | falling
 *   4  |    B      |  A  |   C   | rising
 *   5  |    B      |  C  |   A   | falling
 *   6  |    A      |  C  |   B   | rising
 * ============================================================= */
static inline void comStep(uint8_t s)
{
    switch (s) {
    case 1: phaseC_FLOAT(); phaseB_LOW();  phaseA_PWM();  break;
    case 2: phaseA_FLOAT(); phaseB_LOW();  phaseC_PWM();  break;
    case 3: phaseB_FLOAT(); phaseA_LOW();  phaseC_PWM();  break;
    case 4: phaseC_FLOAT(); phaseA_LOW();  phaseB_PWM();  break;
    case 5: phaseA_FLOAT(); phaseC_LOW();  phaseB_PWM();  break;
    case 6: phaseB_FLOAT(); phaseC_LOW();  phaseA_PWM();  break;
    default: allPhases_FLOAT();                            break;
    }
}

/* Route the floating phase to the comparator and arm the correct edge. */
static inline void changeCompInput(void)
{
    switch (step) {
    case 1: case 4: COMP_SELECT(COMP_INPUT_PHASE_C); break;
    case 2: case 5: COMP_SELECT(COMP_INPUT_PHASE_A); break;
    case 3: case 6: COMP_SELECT(COMP_INPUT_PHASE_B); break;
    }

    /* Odd steps see a falling BEMF in forward, even steps rising.
     * Reverse rotation inverts the expectation. */
    rising = ((step & 1u) == 0u) ^ (!forward);

    /* EXTI is triggered on the *comparator output*, whose polarity is
     * inverted relative to the phase voltage on this wiring:
     * phase rising past neutral => comp output falls. */
    if (rising) COMP_TRIG_FALLING();
    else        COMP_TRIG_RISING();
}

/* ===================== Commutate ===================== */
void commutate(void)
{
    if (forward) {
        step = (step >= 6) ? 1 : (uint8_t)(step + 1);
    } else {
        step = (step <= 1) ? 6 : (uint8_t)(step - 1);
    }

    commutation_intervals[step - 1] = commutation_interval;
    comStep(step);
    changeCompInput();

    if (average_interval > 1700) {   /* low speed: re-validate startup */
        old_routine = true;
    }
    zcfound = false;
}

/* Recompute filter strength and advance from current speed. */
static inline void updateSpeedDependentParams(void)
{
    /* Interval in ticks: shorter => faster => fewer filter samples. */
    if (average_interval < 100)       filter_level = MIN_FILTER_LEVEL;
    else if (average_interval < 400)  filter_level = 4;
    else if (average_interval < 1000) filter_level = 7;
    else                              filter_level = MAX_FILTER_LEVEL;

    /* advance in ticks: interval is 60 electrical deg; level*7.5deg */
    advance  = (commutation_interval >> 3) * advance_level;
    waitTime = (commutation_interval >> 1) - advance;  /* 30deg - advance */
    blankTime = commutation_interval / BLANKING_DIVIS