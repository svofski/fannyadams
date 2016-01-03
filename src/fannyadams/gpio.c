#include <inttypes.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/cm3/nvic.h>

#include "gpio.h"
#include "event.h"

// #define LINEOUT_SW_Pin GPIO_PIN_8
// #define LINEOUT_SW_GPIO_Port GPIOB
// #define PHONES_SW_Pin GPIO_PIN_9
// #define PHONES_SW_GPIO_Port GPIOB

// configure plug switches
// B.8 = LINEOUT connector (plugged = 0)
// B.9 = PHONES connector (plugged = 1)
void GPIO_Setup(void) {
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_SYSCFG);

	gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO8 | GPIO9);
	
	exti_select_source(EXTI8, GPIOB);
	exti_set_trigger(EXTI8, EXTI_TRIGGER_BOTH);
	exti_select_source(EXTI9, GPIOB);
	exti_set_trigger(EXTI9, EXTI_TRIGGER_BOTH);

	exti_enable_request(EXTI8);
	exti_enable_request(EXTI9);
	nvic_enable_irq(NVIC_EXTI9_5_IRQ);
}

int GPIO_IsOutputPlugged() {
	return (GPIOB_IDR & GPIO8) == 0 || (GPIOB_IDR & GPIO9) != 0;
}

void exti9_5_isr(void) {
	uint16_t plugged = (GPIOB_IDR & GPIO8) == 0 || (GPIOB_IDR & GPIO9) != 0;
	Event_Post((Event) {EventId: EVENT_OUTPUT_PLUG, Data:plugged});
	exti_reset_request(EXTI8);
	exti_reset_request(EXTI9);
}