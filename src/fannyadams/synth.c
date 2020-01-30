#include <string.h>
#include <inttypes.h>

#include "util.h"
#include "synth.h"
#include "midi.h"
#include "xprintf.h"
#include "adsr.h"
#include "sintab.h"
const size_t sintab_n = sizeof sintab / sizeof sintab[0];

#define OSC_N 8

#define framesize 48

const int FS = 48000;

osc_t osc[OSC_N];

typedef struct voice
{
    uint8_t osc;
    // adsr
    uint32_t adsr_phase;
} voice_t;

static void note_off(uint8_t chan, uint8_t note, uint8_t velocity);
static void note_on(uint8_t chan, uint8_t note, uint8_t velocity);

void osc_init(osc_t * g)
{
    bzero(g, sizeof *g);
}

void osc_setfreq(osc_t * g, float hz)
{
    // 1hz -> 1 period in FS samples
    // phase = 8.24
    // e.g. 1 Hz, must advance by sintab_n (256) in FS (48000) counts
    // one step is 256/48000
    float step = hz * sintab_n / FS;
    g->phase_inc = (uint32_t) (step * (1 << FIX));
    g->phase = 0;

    xprintf("osc_setfreq: hz=%d inc=%d\n", (int)hz, g->phase_inc);
}

void osc_frame(osc_t * g, int32_t * buf)
{
    uint32_t phase = g->phase;
    uint32_t inc = g->phase_inc;
    uint32_t ph;
    // samples per frame * 2 channels
    for (int i = 0; i < framesize * 2;) {
        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += sintab[ph]; buf[i++] += sintab[ph]; 
        phase = phase + inc;

        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += sintab[ph]; buf[i++] += sintab[ph]; 
        phase = phase + inc;

        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += sintab[ph]; buf[i++] += sintab[ph]; 
        phase = phase + inc;

        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += sintab[ph]; buf[i++] += sintab[ph]; 
        phase = phase + inc;
    }
    g->phase = phase;
}

void synth_init()
{
    midi_note_on_cb = note_on;
    midi_note_off_cb = note_off;
    for (int i = 0; i < OSC_N; ++i) {
        osc_init(&osc[i]);
    }
}

// called every 1ms frame, fills 48 samples
void synth_frame(int32_t * buf)
{
    bzero(buf, framesize * sizeof(int32_t) * 2);
    for (int i = 0; i < OSC_N; ++i) {
        osc_frame(&osc[i], buf);
    }
}

void note_on(uint8_t chan, uint8_t note, uint8_t velocity)
{
    STFU(velocity);
    //if (chan < OSC_N) {
    //    osc_setfreq(&osc[chan], note * 10);
    //}
    for (int i = 0; i < OSC_N; ++i) {
        osc_setfreq(&osc[i], note*10 + i);
    }
}

void note_off(uint8_t chan, uint8_t note, uint8_t velocity)
{
    STFU(velocity); STFU(note);
    //if (chan < OSC_N) {
    //    osc_setfreq(&osc[chan], 0);
    //}
    for (int i = 0; i < OSC_N; ++i) {
        osc_setfreq(&osc[i], 0);
    }
}

#ifdef TEST
int osc_test()
{
    osc_t osc;
    osc_init(&osc);
    osc_setfreq(&osc, 1000);
    int32_t buf[framesize * 2];

    FILE * fo = fopen("test_osc_1.txt", "w");
    fprintf(fo, "#!/usr/bin/env gnuplot\n");
    fprintf(fo, "# test_osc_1 (n left right)\n");
    fprintf(fo, "plot '-' using 1:2 with lines\n");
    for (int frame = 0; frame < 10; ++frame) {
        bzero(buf, sizeof buf);
        osc_frame(&osc, buf);
        for (int i = 0; i < framesize; ++i) {
            fprintf(fo, "%d %d %d\n", frame * framesize + i, buf[i*2], buf[i*2+1]);
        }
    }
    fprintf(fo, "e\npause -1\n");
    fclose(fo);
}
#endif
