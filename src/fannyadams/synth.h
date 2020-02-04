#pragma once

#include "adsr.h"

typedef struct osc {
    uint32_t phase;
    uint32_t phase_inc;
} osc_t;

void osc_init(osc_t * g);
void osc_setfreq(osc_t * g, float hz);
void osc_frame(osc_t * g, int32_t * buf, float volume, adsr_t * env);
void osc_frame_f(osc_t * g, float * buf, float volume, adsr_t * env);

void synth_init(void);
void synth_frame(int32_t * buf);
