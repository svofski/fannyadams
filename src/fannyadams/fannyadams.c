#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>

#include "usrat.h"
#include "xprintf.h"


static void clock_setup(void)
{
    rcc_clock_setup_hse_3v3(&rcc_hse_25mhz_3v3[RCC_CLOCK_3V3_168MHZ]);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOA);

	rcc_periph_clock_enable(RCC_USART1);
}

static void gpio_setup(void)
{
	/* Setup GPIO pin GPIO12 on GPIO port D for LED. */
	//gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
}

int main(void)
{
	clock_setup();
	gpio_setup();
	Debug_UART_Setup();


	xprintf("LES SHADOKS POMPAIENT\n\r");
	while (1) {
		__asm__("NOP");

		if (xavail()) {
			int c = xgetchar();
			xprintf("Pressed: %c [%02x]\n\r", c);
		}
	}

	return 0;
}
