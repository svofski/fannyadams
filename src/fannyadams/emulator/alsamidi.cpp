#include <string.h>
#include <iostream>
#include "alsamidi.h"

using namespace std;

// borrows heavily from amsynth code
// https://github.com/amsynth/amsynth/blob/develop/src/drivers/ALSAMidiDriver.cpp

int AlsaMidi::open()
{
    memset(&pollfd_in, 0, sizeof pollfd_in);
    if (snd_midi_event_new(bufsize, &seq_midi_parser)) {
        cout << "Error creating MIDI event parser\n";
    }
    // disable midi event merging
    snd_midi_event_no_status(seq_midi_parser, 1);

    if (snd_seq_open(
          &seq_handle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) != 0) {
        cerr << "Error opening ALSA sequencer.\n";
        return -1;
    }

    if (seq_handle == NULL) {
        cerr << "error: snd_seq_open() claimed to succeed but seq_handle is "
                "NULL.\n";
        return -1;
    }

    if (client_name) {
        snd_seq_set_client_name(seq_handle, client_name);
    }

    if ((portid = snd_seq_create_simple_port(seq_handle, "MIDI IN",
           SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
           SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {
        cerr << "Error creating sequencer port.\n";
        return -1;
    }

    snd_seq_poll_descriptors(seq_handle, &pollfd_in, 1, POLLIN);

    return 0;
}

int AlsaMidi::read_to_usb(uint32_t* usb_packet)
{
    if (seq_handle == NULL) {
        return 0;
    }
    unsigned char buffer[4];

    snd_seq_event_t* ev = NULL;
    int res = snd_seq_event_input(seq_handle, &ev);
    if (res < 0)
        return -1;

    //print_midi_ev(ev);
    int len = snd_midi_event_decode(seq_midi_parser, &buffer[1], 3, ev);
    buffer[0] = buffer[1] & 0xf0;

    *usb_packet = *((uint32_t*)&buffer[0]);

    return 0;
}

void print_midi_ev(snd_seq_event_t* ev)
{
    switch (ev->type) {
        case SND_SEQ_EVENT_CONTROLLER:
            fprintf(stderr, "Control event on Channel %2d: %5d       \n",
              ev->data.control.channel, ev->data.control.value);
            break;
        case SND_SEQ_EVENT_PITCHBEND:
            fprintf(stderr, "Pitchbender event on Channel %2d: %5d   \n",
              ev->data.control.channel, ev->data.control.value);
            break;
        case SND_SEQ_EVENT_NOTEON:
            fprintf(stderr, "Note On event on Channel %2d: %5d       \n",
              ev->data.control.channel, ev->data.note.note);
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            fprintf(stderr, "Note Off event on Channel %2d: %5d      \n",
              ev->data.control.channel, ev->data.note.note);
            break;
    }
}
