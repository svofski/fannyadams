#pragma once

#include <libopencm3/usb/audio.h>

#define AUDIO_BUFFER_SIZE 1024

void I2S_Setup(void);
void I2S_Start(void);
void I2S_Shutdown(void);
void I2S_InitBuffer(void);
int32_t* I2S_GetBuffer(void);
