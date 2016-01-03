#pragma once

#include <inttypes.h>

#define DEBOUNCE_NBINS 4
enum _debounce_bins {
	DEBOUNCE1 = 0, DEBOUNCE2, DEBOUNCE3, DEBOUNCE4
};
#define DEBOUNCE_POWER 0
#define DEBOUNCE_DCDC 1


void Clock_Setup(void);
uint32_t Clock_Get(void);
void Clock_Debounce(int bin, int millis);