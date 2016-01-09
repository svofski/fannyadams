#pragma once

#include <libopencm3/usb/audio.h>

// 256 * 2 interleaved stereo samples ~ 187ms
#define AUDIO_BUFFER_SIZE ((USB_AUDIO_PACKET_SIZE(48000,2,16) + 4)/2)

void I2S_Setup(void);
void I2S_Start(void);
void I2S_Shutdown(void);
void I2S_InitBuffer(void);
int32_t* I2S_GetBuffer(void);
