#pragma once

#if 0
         /\
       /    \_____
     /             \
   /                 \
   A: rate
   D: rate
   S: value
   R: rate
#endif
typedef struct adsr_
{
    enum { ADSR_A, ADSR_D, ADSR_S, ADSR_R } state;
    float a, d, s, r;
    float v;
} adsr_t;

void adsr_reset(adsr_t * env, float a, float d, float s, float r);
void adsr_step(adsr_t * env, int note_on);
