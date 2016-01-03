#pragma once

typedef enum _power_state {
	POWER_OFF = 0,
	POWER_ON
} PowerState;

void Power_Setup(void);
void Power_Start(void);
void Power_Stop(void);
void Power_SetFactor(int factor);
int Power_GetFactor(void);
void Power_SetPeriod(int period);
int Power_GetPeriod(void);
PowerState Power_GetState(void);