#pragma once

#include <libopencm3/usb/audio.h>

// sample count * 2 for stereo (48khz -> 48*2)
#define I2S_SYNC_PERIOD 1
#define AUDIO_BUFFER_SIZE (48*2*I2S_SYNC_PERIOD) // 2ms = 2 packet
//((USB_AUDIO_PACKET_SIZE(48000,2,16))*2)

typedef void (*dma_callback)(void);

void I2S_Setup(void);
void I2S_Start(void);
void I2S_Pause(void);
void I2S_Shutdown(void);
void I2S_InitBuffer(void);
int32_t* I2S_GetBuffer(void);
void I2S_SetCallback(dma_callback callback);