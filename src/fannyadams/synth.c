#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "util.h"
#include "osc.h"
#include "synth.h"
#include "midi.h"
#include "xprintf.h"
#include "adsr.h"
#include "notefreq.h"
#include "patch.h"

#define OSC_N 16
#define VOICE_N (OSC_N)
#define CHAN_N 16

osc_t osc[OSC_N];

typedef struct voice
{
    midi_note_t note;        /* 0..127 are valid midi notes */
    midi_chan_t chan;
    adsr_t envelope;
    float volume;
    int8_t lru_count;
} voice_t;

typedef struct chan
{
    int bank;

    waveform_t waveform;
    float pwm_compare;
    float a, d, s, r;

    int16_t pitchbend_semitones; // 1 = -1..+1, 2 semitones
    int16_t bend;
} chan_t;

typedef struct synth
{
} synth_t;

voice_t voice[VOICE_N];
chan_t  channel[CHAN_N];
synth_t global;

void voice_init(voice_t * v);
int voice_lru_get(midi_note_t note);
void voice_lru_release(midi_note_t note);
void voice_lru_release_voice(int v);
int voice_lru_keyup(midi_note_t note);

static void note_off(midi_chan_t chan, midi_note_t note, uint8_t velocity);
static void note_on(midi_chan_t chan, midi_note_t note, uint8_t velocity);
static void stfu(midi_chan_t chan);
static void reset_all_controllers(midi_chan_t chan);
static void pitchbend(midi_chan_t chan, int16_t bend);
static void progchange(midi_chan_t chan, midi_prog_t prog);

void synth_init()
{
    midi_note_on_cb = note_on;
    midi_note_off_cb = note_off;
    midi_all_sound_off_cb = stfu;
    midi_all_notes_off_cb = stfu;
    midi_reset_all_cntrls_cb = reset_all_controllers;
    midi_pitchbend_cb = pitchbend;
    midi_program_change_cb = progchange;

    for (int i = 0; i < OSC_N; ++i) {
        osc_init(&osc[i]);
    }
    for (int i = 0; i < VOICE_N; ++i) {
        voice_init(&voice[i]);
    }
    for (int i = 0; i < CHAN_N; ++i) {
        channel[i].bend = 8192;
        channel[i].pitchbend_semitones = 2;

        progchange(i, 0);
    }
}

// called every 1ms frame, fills 48 samples
void synth_frame(int32_t * buf)
{
    osc_zero(buf);
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

    voice[v].envelope.a = channel[chan].a;
    voice[v].envelope.d = channel[chan].d;
    voice[v].envelope.s = channel[chan].s;
    voice[v].envelope.r = channel[chan].r;

    adsr_note_on(&voice[v].envelope);

    osc_setfreq(&osc[v], notefreq(note, channel[chan].bend));
    osc[v].waveform =   channel[chan].waveform;
    osc[v].pwm_compare =channel[chan].pwm_compare;

    //osc[v].waveform = WF_TRIANGLE;

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

void reset_all_controllers(midi_chan_t chan)
{
    channel[chan].bend = 8192;
}

void pitchbend(midi_chan_t chan, int16_t bend)
{
    STFU(chan); STFU(bend);
    channel[chan].bend = bend;
    // update the notes that are currently on
    //xprintf("bend: %d %d\n", chan, bend);
    for (int v = 0; v < VOICE_N; ++v) {
        if (voice[v].chan == chan && voice[v].note >= 0) {
            osc_setfreq(&osc[v], notefreq(voice[v].note, bend));
        }
    }
}

void progchange(midi_chan_t chan, midi_prog_t prog)
{
    const patch_t * patch = &bank_gm[prog];

    channel[chan].waveform = patch->waveform;
    channel[chan].pwm_compare = patch->pwm_compare / 255.f;
    // a: 1 = slow, 15 = instant
    channel[chan].a = 0.000001 * expf(patch->a/.5f);
    channel[chan].d = 0.000001 * expf(patch->d/.5f);
    channel[chan].s = patch->s / 15.f;
    channel[chan].r = 0.000001 * expf(patch->r/1.f);
}

#ifdef TEST

#include <stdlib.h>
#include "test/test.h"

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
    int32_t buf[FRAMESIZE * 2];
    synth_init();

    printf("Press A1 (880 Hz) Midi note 80\n");

    const int frame_on = 1;
    const int frame_off = 20000/48;
    const int frame_total = 24000/48;

    FILE * fo = fopen_exe("note_test1.txt");
    gnuplot_plot_headers(fo, "keypress\\_test() ADSR envelope test",
            "Play A1 880 Hz", "1:2", "sample", "volume");
    for (int frame = 0; frame < frame_total; ++frame) {
        bzero(buf, sizeof buf);
        synth_frame(buf);

        for (int i = 0; i < FRAMESIZE; ++i) {
            fprintf(fo, "%d %d %d\n", frame * FRAMESIZE + i, buf[i*2], buf[i*2+1]);
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
