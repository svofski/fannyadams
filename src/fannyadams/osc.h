#pragma once

#include "adsr.h"

typedef enum {
    WF_SIN,
    WF_SAW,
    WF_TRIANGLE,
    WF_PWM,
} waveform_t;

typedef struct osc {
    uint32_t phase;
    uint32_t phase_inc;
    waveform_t waveform;
    float pwm_compare;
} osc_t;

void osc_init(osc_t * g);
void osc_zero(int32_t * buf);
void osc_setfreq(osc_t * g, float hz);
void osc_frame(osc_t * g, int32_t * buf, float volume, adsr_t * env);
