#pragma once

typedef struct osc {
    uint32_t phase;
    uint32_t phase_inc;
} osc_t;

void osc_init(osc_t * g);
void osc_setfreq(osc_t * g, float hz);
void osc_frame(osc_t * g, int32_t * buf);

void synth_init(void);
void synth_frame(int32_t * buf);
