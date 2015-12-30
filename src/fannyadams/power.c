#include <inttypes.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>

#include "power.h"

#define PWM_PUSH_Pin GPIO_PIN_10
#define PWM_PUSH_GPIO_Port GPIOA

#define PWM_PULL_Pin GPIO_PIN_1
#define PWM_PULL_GPIO_Port GPIOB

#define MIN_DEADTIME (0000 + 32)

static const uint8_t power_table[8] = {
	0300 + 0,
	0200 + 48,
	0200 + 32,
	0200 + 16,
	0200 + 0,
	0100 + 32,
	0100 + 0,
	MIN_DEADTIME};  // 7

static volatile uint8_t dead_set = MIN_DEADTIME;


#define PWM_FREQ	100000ul
#define PERIOD (168000000ul/PWM_FREQ/2 - 1)
#define HALFPERIOD (PERIOD/2)

static void Reconfigure(void) {
	timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_CENTER_1, TIM_CR1_DIR_UP);
	timer_continuous_mode(TIM1);

	timer_disable_break(TIM1);

	timer_set_oc_value(TIM1, TIM_OC1, 0);
	timer_set_oc_value(TIM1, TIM_OC3, HALFPERIOD);
	timer_set_period(TIM1, PERIOD);
 	timer_set_deadtime(TIM1, dead_set);

	timer_set_oc_mode(TIM1, TIM_OC1, TIM_OCM_FROZEN);
	timer_set_oc_mode(TIM1, TIM_OC3, TIM_OCM_PWM2);

	timer_set_oc_polarity_high(TIM1, TIM_OC3);
	timer_set_oc_idle_state_unset(TIM1, TIM_OC3);

	timer_set_oc_polarity_high(TIM1, TIM_OC3N);
	timer_set_oc_idle_state_unset(TIM1, TIM_OC3N);	
	
	timer_enable_oc_output(TIM1, TIM_OC3);
	timer_enable_oc_output(TIM1, TIM_OC3N);

	// outputs will be enabled on the next update event
	timer_enable_break_automatic_output(TIM1);
}

void Power_Setup(void) {
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
	//timer_enable_break_main_output(TIM1); 
	timer_enable_counter(TIM1);	
}

void Power_Stop(void) {
	timer_disable_break_main_output(TIM1); 
	timer_disable_counter(TIM1);
}

void Power_Scale(int factor) {
	uint8_t f;
	if (factor < 0) {
		f = 0;
	} else if (factor > 7) {
		f = 7;
	} else {
		f = (uint8_t) factor;
	}
	dead_set = power_table[f];
	xprintf("Power_Scale(%d) deadtime=%d\n\r", factor, dead_set);
	timer_clear_flag(TIM1, TIM_SR_UIF);
	timer_enable_irq(TIM1, TIM_DIER_UIE);	
}

void tim1_up_tim10_isr(void) {
	timer_clear_flag(TIM1, TIM_SR_UIF); 	// clear interrupt flag
	timer_disable_break_main_output(TIM1); 
	timer_reset(TIM1); 						// completely reset the timer, otherwise deadtime cannot be set
	Reconfigure(); 							// set up everything
	timer_enable_break_main_output(TIM1);
	timer_enable_counter(TIM1); 			// start the timer again
}