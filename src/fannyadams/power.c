#include <inttypes.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>

#include "power.h"

// PUSH is A.10
// PULL is B.1

#define MAX_DEADTIME (0300 + 0)
#define MIN_DEADTIME (0000 + 32)

static const uint8_t power_table[8] = {
    MAX_DEADTIME,       // minimal power: ~ 50% duty
    0200 + 48,
    0200 + 32,
    0200 + 16,
    0200 + 0,
    0100 + 32,
    0100 + 0,
    MIN_DEADTIME};  // maximum power

//#define PWM_FREQ    183000ul
#define PWM_FREQ    133000ul
#define PERIOD      (168000000ul/PWM_FREQ/2 - 1)

static volatile uint8_t dead_set = MAX_DEADTIME;
static volatile uint8_t factor = 4;
static volatile uint16_t period = PERIOD;

static volatile PowerState state;

static void Reconfigure(void) {
    // no prescaler (168MHz clock)
    timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_CENTER_1, TIM_CR1_DIR_UP);  // todo: probably edge aligned is easier 
    timer_continuous_mode(TIM1);                        // keeps on running

    timer_disable_break(TIM1);                          // break function not used

    timer_set_oc_mode(TIM1, TIM_OC1, TIM_OCM_FROZEN);   // OC1 used for timing base
    timer_set_oc_mode(TIM1, TIM_OC3, TIM_OCM_PWM2);     // OC3 is the PWM channel

    timer_set_oc_value(TIM1, TIM_OC1, 0);               // OC1 is frozen
    timer_set_oc_value(TIM1, TIM_OC3, period >> 1);     // OC3 is the main pwm, always 50% for simmetry
    timer_set_period(TIM1, period);                     // sets the frequency
    timer_set_deadtime(TIM1, dead_set);                 // dead-time used for protection and duty cycle

    timer_set_oc_polarity_high(TIM1, TIM_OC3);          // _|     |_________  high when active
    timer_set_oc_idle_state_unset(TIM1, TIM_OC3);       // ____ zero when MOE = 0 ___

    timer_set_oc_polarity_high(TIM1, TIM_OC3N);         // _________|     |__ high when active
    timer_set_oc_idle_state_unset(TIM1, TIM_OC3N);      // ____ zero when MOE = 0 ___
    
    timer_enable_oc_output(TIM1, TIM_OC3);              // enable OC3 
    timer_enable_oc_output(TIM1, TIM_OC3N);             // and complementary OC3N

    timer_enable_break_automatic_output(TIM1);          // AOE: outputs are disabled but will be enabled 
                                                        // on the next update event

    timer_enable_oc_preload(TIM1, TIM_OC3);
    timer_enable_preload(TIM1);
}

void Power_Setup(void) {
    period = PERIOD;
    factor = 4;
    dead_set = power_table[factor];

    // GPIO Pins
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);

    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO10);
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO1);

    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO10);
    gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO1);

    gpio_set_af(GPIOA, GPIO_AF1, GPIO10); // PA.10 = TIM1_CH3
    gpio_set_af(GPIOB, GPIO_AF1, GPIO1);  // PB.1  = TIM1_CH3N

    // Enable TIM1 power, reset and reconfigure
    rcc_periph_clock_enable(RCC_TIM1);
    timer_reset(TIM1);
    Reconfigure();

    // interrupt for power adjustments
    nvic_enable_irq(NVIC_TIM1_UP_TIM10_IRQ);
}

void Power_Start(void) {
    //timer_enable_break_main_output(TIM1); // not necessary, because:
    timer_enable_counter(TIM1);             // AOE setting will set MOE on first update event

    state = POWER_ON;
}

void Power_Stop(void) {
    timer_disable_break_main_output(TIM1);  // force idle state on outputs (unset, low)
    timer_disable_counter(TIM1);            // stop

    state = POWER_OFF;
}

void Power_SetFactor(int newfactor) {
    factor = newfactor < 0 ? 0 : newfactor > 7 ? 7 : (uint8_t) newfactor;
    dead_set = power_table[factor];
    xprintf("Power_Scale(%d) deadtime=%d\n\r", factor, dead_set);
    timer_clear_flag(TIM1, TIM_SR_UIF);
    timer_enable_irq(TIM1, TIM_DIER_UIE);   
}

int Power_GetFactor() {
    return factor;
}

void Power_SetPeriod(int newperiod) {
    // use reload thingies?
    period = newperiod < 0 ? 0 : newperiod > 65535 ? 65535 : newperiod;
    timer_set_oc_value(TIM1, TIM_OC3, period >> 1);     // OC3 is the main pwm, always 50% for simmetry
    timer_set_period(TIM1, period);                     // sets the frequency    
}

int Power_GetPeriod() {
    return period;
}

PowerState Power_GetState(void) {
    return state;
}

// The reference (17.3.11) says that
// Dead-time insertion is enabled by setting both CCxE and CCxNE bits, and the MOE bit if the
// break circuit is present. 
// todo: try to clear CC3E/CC3NE and set again, see what happens
void tim1_up_tim10_isr(void) {
    timer_clear_flag(TIM1, TIM_SR_UIF);     // clear interrupt flag
    timer_disable_break_main_output(TIM1);  // set outputs to idle mode (both channels low)
    timer_reset(TIM1);                      // completely reset the timer, otherwise deadtime cannot be set
    Reconfigure();                          // set up everything
    timer_enable_break_main_output(TIM1);   // force master output enable (MOE) without waiting for auto
    timer_enable_counter(TIM1);             // start the timer again
}