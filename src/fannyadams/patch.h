#pragma once

#include "adsr.h"
#include "osc.h"

typedef struct patch
{
    waveform_t waveform;
    uint8_t pwm_compare;
    uint8_t a, d, s, r; /* opl2-like 0..15 */
} patch_t;

extern const patch_t bank_gm[128];
extern const patch_t bank_drums[128];

