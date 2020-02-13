#pragma once

#include "adsr.h"

#define FRAMESIZE 48
#define FS 48000

void synth_init(void);
void synth_frame(int32_t * buf);
