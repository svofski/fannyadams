#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "adsr.h"
#include "synth.h"

#include "sintab.h"
const size_t sintab_n = sizeof sintab / sizeof sintab[0];

void osc_frame_sin(osc_t * g, int32_t * buf, float volume, adsr_t * env);
void osc_frame_saw(osc_t * g, int32_t * buf, float volume, adsr_t * env);
void osc_frame_triangle(osc_t * g, int32_t * buf, float volume, adsr_t * env);
void osc_frame_pwm(osc_t * g, int32_t * buf, float volume, adsr_t * env);

void osc_init(osc_t * g)
{
    bzero(g, sizeof *g);
    g->pwm_compare = 0.5;
}

void osc_zero(int32_t * buf)
{
    bzero(buf, FRAMESIZE * sizeof(int32_t) * 2);
}

void osc_setfreq(osc_t * g, float hz)
{
    // 1hz -> 1 period in FS samples
    // phase = 8.24
    // e.g. 1 Hz, must advance by sintab_n (256) in FS (48000) counts
    // one step is 256/48000
    float step = hz * sintab_n / FS;
    g->phase_inc = (uint32_t) (step * (1 << FIX));
}

void osc_frame(osc_t * g, int32_t * buf, float volume, adsr_t * env)
{
    // not nice but seems to save us from some critical timing problems at start
    if (volume == 0) return;

    switch (g->waveform) {
        case WF_SIN: 
            osc_frame_sin(g, buf, volume, env);
            break;
        case WF_SAW:
            osc_frame_saw(g, buf, volume, env);
            break;
        case WF_TRIANGLE:
            osc_frame_triangle(g, buf, volume, env);
            break;
        case WF_PWM:
            osc_frame_pwm(g, buf, volume, env);
            break;
    }
}

void osc_frame_sin(osc_t * g, int32_t * buf, float volume, adsr_t * env)
{
    uint32_t phase = g->phase;
    uint32_t inc = g->phase_inc;
    uint32_t ph;
    float v;

    // samples per frame * 2 channels
    for (int i = 0; i < FRAMESIZE * 2;) {
        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask;
        int32_t pcm = v * sintab[ph];
        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;

        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask;
        pcm = v * sintab[ph];
        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;

        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask;
        pcm = v * sintab[ph];
        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;

        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask;
        pcm = v * sintab[ph];
        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;
    }
    g->phase = phase;
}

void osc_frame_saw(osc_t * g, int32_t * buf, float volume, adsr_t * env)
{
    uint32_t phase = g->phase;
    uint32_t inc = g->phase_inc;
    uint32_t ph;
    float v;

    // samples per frame * 2 channels
    for (int i = 0; i < FRAMESIZE * 2;) {
        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask;
        int32_t pcm = v * (ph * 2 / (1.0f+sintab_mask) - 1.0f);
        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;
    }
    g->phase = phase;
}

void osc_frame_triangle(osc_t * g, int32_t * buf, float volume, adsr_t * env)
{
    // not nice but seems to save us from some critical timing problems at start
    if (volume == 0) return;

    uint32_t phase = g->phase;
    uint32_t inc = g->phase_inc;
    uint32_t ph;
    float v;
    int32_t pcm;

    // samples per frame * 2 channels
    for (int i = 0; i < FRAMESIZE * 2;) {
        adsr_step(env); v = volume/(1 + sintab_mask) * env->v;
        ph = (phase >> FIX) & sintab_mask;

        pcm = ph << 2;
        if (ph > 3*sintab_mask/4) {
            pcm = pcm - sintab_mask * 4;
        }
        else if (ph > sintab_mask/4) {
            pcm = -pcm + sintab_mask*2;
        }

        pcm *= v;

        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;
    }
    g->phase = phase;
}

void osc_frame_pwm(osc_t * g, int32_t * buf, float volume, adsr_t * env)
{
    // not nice but seems to save us from some critical timing problems at start
    if (volume == 0) return;

    uint32_t phase = g->phase;
    uint32_t inc = g->phase_inc;
    uint32_t ph;
    float v;
    int32_t pcm;
    float compare = g->pwm_compare;

    // samples per frame * 2 channels
    for (int i = 0; i < FRAMESIZE * 2;) {
        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask;

        pcm = ph/(1.0f+sintab_mask) > compare ? v : -v;
        buf[i++] += pcm; buf[i++] += pcm;
        phase = phase + inc;
    }
    g->phase = phase;
}

#ifdef TEST

#include <stdlib.h>
#include "test/test.h"

int osc_test1()
{
    osc_t osc[4];
    for (int i = 0; i < 4; ++i) {
        osc_setfreq(&osc[i], 200);
        osc[i].waveform = (waveform_t)i;
        osc[i].phase = 0;
        osc[i].pwm_compare = 0.5;
    }

    int32_t buf[4][FRAMESIZE * 2];

    FILE * fo = fopen_exe("osc_test1.txt");

    const char * labels[] = {"sin", "saw", "tri", "pwm"};
    gnuplot_plot_headers4(fo, 
            /* title */ "osc\\_test1(): Simple sin oscillator test",
            "sample", "volume");
    
    adsr_t env;
    adsr_reset(&env, 1, 0, 1, 1);
    adsr_note_on(&env);

    fprintf(fo, "$data << EOF\n");

    for (int frame = 0; frame < 10; ++frame) {
        for (int i = 0; i < 4; ++i) {
            bzero(buf[i], sizeof buf[i]);
            osc_frame(&osc[i], buf[i], 4096, &env);
        }

        for (int i = 0; i < FRAMESIZE; ++i) {
            fprintf(fo, "%d %d %d %d %d\n", frame * FRAMESIZE + i, 
                    buf[0][i*2], buf[1][i*2], buf[2][i*2], buf[3][i*2]);
        }
    }
    fprintf(fo, "EOF\n");
    gnuplot_plot4(fo, labels);
    fclose(fo);
    system("./osc_test1.txt");

    return 0;
}

int osc_test2()
{
    osc_t osc;
    osc_init(&osc);
    int32_t buf[FRAMESIZE * 2];

    adsr_t env;
    adsr_reset(&env, 1, 0, 1, 1);
    adsr_note_on(&env);

    osc.waveform = WF_SIN;

    FILE * fo = fopen_exe("osc_test2.txt");
    gnuplot_plot_headers(fo, "osc\\_test2(): sin sweep 100-1000 Hz",
            "sin sweep", "1:2", "sample", "volume");
    for (int frame = 0; frame < 20; ++frame) {
        bzero(buf, sizeof buf);
        osc_setfreq(&osc, 100 + frame * 45);
        osc_frame(&osc, buf, 4096, &env);
        for (int i = 0; i < FRAMESIZE; ++i) {
            fprintf(fo, "%d %d %d\n", frame * FRAMESIZE + i, buf[i*2], buf[i*2+1]);
        }
    }
    fprintf(fo, "e\n#pause -1\n");
    fclose(fo);
    system("./osc_test2.txt");

    return 0;
}

int osc_test()
{
    return 
            osc_test1() | 
            osc_test2();
}

#endif
