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

typedef enum { ADSR_0, ADSR_A, ADSR_D, ADSR_S, ADSR_R } state_t;

typedef struct adsr_
{
    state_t state;
    float a, d, s, r;
    float v;
} adsr_t;

void adsr_reset(adsr_t * env, float a, float d, float s, float r);
void adsr_step(adsr_t * env);
void adsr_note_on(adsr_t * env);
void adsr_note_off(adsr_t * env);
