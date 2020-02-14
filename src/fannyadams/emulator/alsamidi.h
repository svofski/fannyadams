#pragma once

#include <poll.h>
#include <alsa/asoundlib.h>

class AlsaMidi
{
    int portid;
    const char* client_name = "Fake Fanny Adams";
    snd_seq_t* seq_handle;
    snd_midi_event_t* seq_midi_parser;
    struct pollfd pollfd_in;
    const size_t bufsize = 32;

  public:
    int open();
    int read_to_usb(uint32_t* usb_packet);
};

void print_midi_ev(snd_seq_event_t* ev);
