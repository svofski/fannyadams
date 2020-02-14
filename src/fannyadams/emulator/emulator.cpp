#include <alsa/asoundlib.h>
#include <unistd.h>
#include <iostream>
#include <math.h>

#include "alsaaudio.h"
#include "alsamidi.h"

#include "midi.h"
#include "synth.h"

using namespace std;

int main()
{
    AlsaMidi midi;
    ALSAAudioDriver audio;

    if (midi.open()) {
        cerr << "Could not open MIDI synthesizer\n";
        return -1;
    }

    if (audio.open()) {
        cerr << "Could not open ALSA audio driver\n";
        return -1;
    }

    synth_init();

    const int nsamples = 48 * 2;
    float audiobuf[nsamples];
    int32_t fannyframe[48 * 2];

    do {
        synth_frame(fannyframe);
        for (int i = 0; i < nsamples; ++i) {
            audiobuf[i] = fannyframe[i] / 32768.0;
        }
        audio.write(audiobuf, nsamples);

        uint32_t usbpacket;
        while (midi.read_to_usb(&usbpacket) == 0) {
            midi_read_usbpacket(usbpacket);
        }
    } while (1);

    return 0;
}
