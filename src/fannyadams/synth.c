#include <string.h>
#include <inttypes.h>

#include "util.h"
#include "synth.h"
#include "midi.h"
#include "xprintf.h"
#include "adsr.h"
#include "notefreq.h"
#include "sintab.h"
const size_t sintab_n = sizeof sintab / sizeof sintab[0];

#define OSC_N 16
#define VOICE_N (OSC_N)

#define framesize 48

const int FS = 48000;

osc_t osc[OSC_N];

typedef struct voice
{
    midi_note_t note;        /* 0..127 are valid midi notes */
    midi_chan_t chan;
    adsr_t envelope;
    float volume;
    int8_t lru_count;
} voice_t;

voice_t voice[VOICE_N];
void voice_init(voice_t * v);
int voice_lru_get(midi_note_t note);
void voice_lru_release(midi_note_t note);
void voice_lru_release_voice(int v);
int voice_lru_keyup(midi_note_t note);

static void note_off(midi_chan_t chan, midi_note_t note, uint8_t velocity);
static void note_on(midi_chan_t chan, midi_note_t note, uint8_t velocity);
static void stfu(midi_chan_t chan);
static void pitchbend(midi_chan_t chan, int16_t bend);

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
}

void osc_frame(osc_t * g, int32_t * buf, float volume, adsr_t * env)
{
    // not nice but seems to save us from some critical timing problems at start
    if (volume == 0) return;

    uint32_t phase = g->phase;
    uint32_t inc = g->phase_inc;
    uint32_t ph;
    float v;

    // samples per frame * 2 channels
    for (int i = 0; i < framesize * 2;) {
        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += v * sintab[ph]; buf[i++] += v * sintab[ph]; 
        phase = phase + inc;

        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += v * sintab[ph]; buf[i++] += v * sintab[ph]; 
        phase = phase + inc;

        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += v * sintab[ph]; buf[i++] += v * sintab[ph]; 
        phase = phase + inc;

        adsr_step(env); v = volume * env->v;
        ph = (phase >> FIX) & sintab_mask; 
        buf[i++] += v * sintab[ph]; buf[i++] += v * sintab[ph]; 
        phase = phase + inc;
    }
    g->phase = phase;
}

void synth_init()
{
    midi_note_on_cb = note_on;
    midi_note_off_cb = note_off;
    midi_all_sound_off_cb = stfu;
    midi_all_notes_off_cb = stfu;
    midi_reset_all_cntrls_cb = stfu;
    midi_pitchbend_cb = pitchbend;

    for (int i = 0; i < OSC_N; ++i) {
        osc_init(&osc[i]);
    }
    for (int i = 0; i < VOICE_N; ++i) {
        voice_init(&voice[i]);
    }
}

// called every 1ms frame, fills 48 samples
void synth_frame(int32_t * buf)
{
    bzero(buf, framesize * sizeof(int32_t) * 2);
    for (int i = 0; i < VOICE_N; ++i) {
        osc_frame(&osc[i], buf, voice[i].volume, &voice[i].envelope);

        if (voice[i].note == -2 && voice[i].envelope.v == 0) {
            voice_lru_release_voice(i);
        }
    }
}

void voice_init(voice_t * v)
{
    adsr_reset(&v->envelope, 0.01/48, 0.01/48, 0.6, 0.01/48);
    v->lru_count = 0;
    v->note = -1;
}

int voice_lru_get(midi_note_t note)
{
    int selected = 0;
    int min = 1000;
    // step 1: decrement all channels, pick last one with zero count
    for (int i = 0; i < VOICE_N; ++i) {
        --voice[i].lru_count;
        if (voice[i].lru_count < min) {
            min = voice[i].lru_count;
            selected = i;
        }
    }
    // step 2: the picked one gets lru_count = VOICE_N
    if (voice[selected].note >= 0) {
        xprintf("lru: expulsed note %d from voice %d\n", voice[selected].note,
                selected);
    }
    voice[selected].lru_count = VOICE_N;
    voice[selected].note = note;
    return selected;
}

void voice_lru_release(midi_note_t note)
{
    for (int i = 0; i < VOICE_N; ++i) {
        if (voice[i].note == note) {
            voice_lru_release_voice(i);
        }
    }
}

void voice_lru_release_voice(int v)
{
    for (int j = 0; j < VOICE_N; ++j) {
        voice[j].lru_count += 1;
    }

    voice[v].note = -1;
    voice[v].lru_count = 0;
}

int voice_lru_keyup(midi_note_t note)
{
    for (int i = 0; i < VOICE_N; ++i) {
        if (voice[i].note == note) {
            voice[i].note = -2; // enter R state of ADSR
            return i;
        }
    }
    return -1;
}

void note_on(midi_chan_t chan, midi_note_t note, uint8_t velocity)
{
    STFU(velocity);
    STFU(chan);

    if (chan == 9) {
        return;
    }

    int v = voice_lru_get(note); // get least recently used voice
    adsr_note_on(&voice[v].envelope);
    osc_setfreq(&osc[v], notefreq(note));
    voice[v].volume = 4096.0f/128.0f * velocity;
    voice[v].chan = chan;
}

void note_off(midi_chan_t chan, midi_note_t note, uint8_t velocity)
{
    STFU(chan); STFU(velocity); STFU(note);

    if (chan == 9) {
        return;
    }

    int v = voice_lru_keyup(note);
    if (v >= 0) {
        adsr_note_off(&voice[v].envelope);
    }
}

void stfu(midi_chan_t chan)
{
    for (int i = 0; i < VOICE_N; ++i) {
        if (voice[i].chan == chan && voice[i].note >= 0) {
            note_off(chan, voice[i].note, 0);
        }
    }
}

void pitchbend(midi_chan_t chan, int16_t bend)
{
    STFU(chan); STFU(bend);
}

#ifdef TEST

#include "stdlib.h"
#include "test/test.h"

int osc_test1()
{
    osc_t osc;
    osc_init(&osc);
    osc_setfreq(&osc, 1000);
    int32_t buf[framesize * 2];

    FILE * fo = fopen_exe("osc_test1.txt");
    gnuplot_plot_headers(fo, "osc\\_test1(): Simple sin oscillator test",
            "sin 1kHz", "1:2", "sample", "volume");
    
    adsr_t env;
    adsr_reset(&env, 1, 0, 1, 1);
    adsr_note_on(&env);

    for (int frame = 0; frame < 10; ++frame) {
        bzero(buf, sizeof buf);
        osc_frame(&osc, buf, 4096, &env);
        for (int i = 0; i < framesize; ++i) {
            fprintf(fo, "%d %d %d\n", frame * framesize + i, buf[i*2], buf[i*2+1]);
        }
    }
    fprintf(fo, "e\n#pause -1\n");
    fclose(fo);
    system("./osc_test1.txt");

    return 0;
}

int osc_test2()
{
    osc_t osc;
    osc_init(&osc);
    int32_t buf[framesize * 2];

    adsr_t env;
    adsr_reset(&env, 1, 0, 1, 1);
    adsr_note_on(&env);

    FILE * fo = fopen_exe("osc_test2.txt");
    gnuplot_plot_headers(fo, "osc\\_test2(): sin sweep 100-1000 Hz",
            "sin sweep", "1:2", "sample", "volume");
    for (int frame = 0; frame < 20; ++frame) {
        bzero(buf, sizeof buf);
        osc_setfreq(&osc, 100 + frame * 45);
        osc_frame(&osc, buf, 4096, &env);
        for (int i = 0; i < framesize; ++i) {
            fprintf(fo, "%d %d %d\n", frame * framesize + i, buf[i*2], buf[i*2+1]);
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

int print_voices(const char * title)
{
    printf("%s\n", title);
    for (int i = 0; i < VOICE_N; ++i) {
        printf("  note: %d lru: %d\n", voice[i].note, voice[i].lru_count);
    }
}

int voice_test()
{
    synth_init();
    printf("1. keep pressing the keys\n");
    for (int i = 0; i < 8; ++i) {
        printf("press note %d, selected voice=%d\n", i, voice_lru_get(i));
    }

    printf("2. previous keys held down, tremolo with one new key\n"); 
    print_voices("Current voices:");

    for (int i = 0; i < 4; ++i) {
        int voice = voice_lru_get(10);
        //print_voices("pressed 10:");
        voice_lru_release(10);
        //print_voices("released 10:");
    }

    print_voices("After tremolo voices:");
    printf("--\n");
}

int keypress_test()
{
    int32_t buf[framesize * 2];
    synth_init();

    printf("Press A1 (880 Hz) Midi note 80\n");

    const int frame_on = 1;
    const int frame_off = 200;
    const int frame_total = 280;

    FILE * fo = fopen_exe("note_test1.txt");
    gnuplot_plot_headers(fo, "keypress\\_test() ADSR envelope test",
            "Play A1 880 Hz", "1:2", "sample", "volume");
    for (int frame = 0; frame < frame_total; ++frame) {
        bzero(buf, sizeof buf);
        synth_frame(buf);

        for (int i = 0; i < framesize; ++i) {
            fprintf(fo, "%d %d %d\n", frame * framesize + i, buf[i*2], buf[i*2+1]);
        }

        if (frame == frame_on) {
            note_on(0, 80, 127);
        }
        else if (frame == frame_off) {
            note_off(0, 80, 0);
        }
    }
    fprintf(fo, "e\n#pause -1\n");
    fclose(fo);
    system("./note_test1.txt");

    printf("--\n");
}
#endif
