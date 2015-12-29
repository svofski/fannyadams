#pragma once

#ifndef ADC3_C
extern volatile int16_t ADC3_BUFFER[3];
#endif

void ADC3_Setup(void);
void ADC3_Start(void);
