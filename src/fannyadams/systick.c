#include <inttypes.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/cortex.h>

#include "systick.h"
#include "event.h"

static volatile uint32_t system_millis;

static volatile int32_t debounce_active;
static volatile int32_t debounce[DEBOUNCE_NBINS];

void sys_tick_handler(void) {
	system_millis++;
	if (debounce_active) {
		debounce_active = 0;
		for (int i = 0; i < DEBOUNCE_NBINS; i++) {
			if (debounce[i] == 0) {
				Event_Post((Event){EventId:EVENT_DEBOUNCE, Data:i});
				debounce[i] = -1;
			} else  if (debounce[i] > 0) {
				--debounce[i];
				debounce_active++;
			}
		}
	}
}

// Set up a timer to create 1mS ticks.
void Clock_Setup(void) {
    rcc_clock_setup_hse_3v3(&rcc_hse_25mhz_3v3[RCC_CLOCK_3V3_168MHZ]);

    for (int i = 0; i < DEBOUNCE_NBINS; i++) {
    	debounce[i] = -1;
    }
    debounce_active = 0;

	// clock rate / 1000 to get 1mS interrupt rate 
	systick_set_reload(168000);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_counter_enable();
	systick_interrupt_enable();
}

uint32_t Clock_Get() {
	return system_millis;
}

void Clock_Debounce(int bin, int millis) {
	debounce[bin] = millis;
	int active = 0;
	for (int i = 0; i < DEBOUNCE_NBINS; i++) {
		if (debounce[bin] >= 0) {
			active++;
		}
	}
	cm_disable_interrupts();
	debounce_active = active;
	cm_enable_interrupts();
}