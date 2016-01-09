#pragma once

// 256 * 2 interleaved stereo samples ~ 187ms
#define AUDIO_BUFFER_SIZE 512

void I2S_Setup(void);
void I2S_Start(void);
void I2S_Shutdown(void);
void I2S_InitBuffer(void);