#include <inttypes.h>
#include <stdio.h>

#include "xprintf.h"
#include "midi.h"

// Code Index Number, table 4-1
enum usbmidi_cin {
   CIN_RESERVED = 0,
   CIN_CABLE_EVENTS = 1,
   CIN_2B_SYSEX = 2,
   CIN_3B_SYSEX = 3,
   CIN_3B_SYSEX_STARTS = 4, // or continues
   CIN_1B_SYSEX_ENDS = 5,
   CIN_1B_SYSTEM_COMMON_MESSAGE = 5,
   CIN_2B_SYSEX_ENDS = 6,
   CIN_3B_SYSEX_ENDS = 7,
   CIN_3B_NOTEOFF = 8,
   CIN_3B_NOTEON = 9,
   CIN_3B_POLYKEYPRESS = 0xa,
   CIN_3B_CONTROL_CHANGE = 0xb,
   CIN_2B_PROGRAM_CHANGE = 0xc,
   CIN_2B_CHANNEL_PRESSURE = 0xd,
   CIN_3B_PITCHBEND_CHANGE = 0xe,
   CIN_1B_SINGLE_BYTE = 0xf
};

enum midi_channel_voice_msg {
    MIDI_NOTEON = 9,
    MIDI_NOTEOFF= 8,
    MIDI_POLYKEYPRESSURE = 0xa,
    MIDI_CONTROL_CHANGE = 0xb,
    MIDI_PROGRAM_CHANGE = 0xc,
    MIDI_CHANNEL_PRESSURE = 0xd,
    MIDI_PITCH_BEND = 0xe,
};

// Cable number is the number of MIDI Jack
typedef union _usbmidi_event_packet
{
    struct __attribute__((packed))
    {
        uint8_t header; // cable_number:4 | code_index_number (cin):4
        uint8_t midi[3];
    };
    uint32_t asuint32;
} usbmidi_event_packet_t;

#define status_code(s)  (0x0f & ((s)>>4))
#define status_chan(s)  (0x0f & (s))

midi_note_onoff_cb_t midi_note_on_cb;
midi_note_onoff_cb_t midi_note_off_cb;

static
void midi_note_on(uint8_t chan, uint8_t note, uint8_t velocity);

static
void midi_note_off(uint8_t chan, uint8_t note, uint8_t velocity);

static
void midi_control_change(uint8_t chan, uint8_t control, uint8_t value);

static
void midi_pitch_bend(uint8_t chan, int16_t value);

void midi_read_usbpacket(uint32_t packet32)
{
    usbmidi_event_packet_t  p;
    p.asuint32 = packet32;


    switch (status_code(p.midi[0])) {
        case MIDI_NOTEON:
            midi_note_on(status_chan(p.midi[0]), p.midi[1], p.midi[2]);
            break;
        case MIDI_NOTEOFF:
            midi_note_off(status_chan(p.midi[0]), p.midi[1], p.midi[2]);
            break;
        case MIDI_CONTROL_CHANGE:
            midi_control_change(status_chan(p.midi[0]), p.midi[1], p.midi[2]);
            break;
        case MIDI_PITCH_BEND:
            {
                int16_t value = ((p.midi[2] << 7) | (p.midi[1])) - 8192;
                midi_pitch_bend(status_chan(p.midi[0]), value);
            }
            break;
        default:
            xprintf("midi_read: cable=%x cin=%x midi: %02x %02x %02x\n",
                    0x0f & (p.header>>4),
                    0x0f & (p.header),
                    p.midi[0], p.midi[1], p.midi[2]);
    }
}

void midi_note_on(uint8_t chan, uint8_t note, uint8_t velocity)
{
    xprintf("NoteOn  %2d  %d %d\n", chan, note, velocity);
    if (midi_note_on_cb) {
        midi_note_on_cb(chan, note, velocity);
    }
}

void midi_note_off(uint8_t chan, uint8_t note, uint8_t velocity)
{
    xprintf("NoteOff %2d  %d %d\n", chan, note, velocity);
    if (midi_note_off_cb) {
        midi_note_off_cb(chan, note, velocity);
    }
}

void midi_control_change(uint8_t chan, uint8_t control, uint8_t value)
{
    const char * ccname = NULL;
    int all_off = 0;

    switch (control) {
        case 120:   ccname = "All Sound Off"; break;
        case 121:   ccname = "Reset All Controllers"; break;
        case 122:   ccname = "Local Control"; break;
        case 123:   ccname = "All Notes Off"; break;
        case 124:   ccname = "Omni Off"; break;
        case 125:   ccname = "Omni On"; break;
        case 126:   ccname = "Mono On (Poly Off)"; break;
        case 127:   ccname = "Poly On (Mono Off)"; break;
    }
    if (control >= 123 && control <= 127) all_off = 1;

    if (ccname) {
        xprintf("CC[%d] %d %d: %s %s\n", chan, control, value, ccname, 
                all_off ? "also All Notes Off" : "");
    }
    else {
        xprintf("CC[%d] %d %d\n", chan, control, value);
    }
}

void midi_pitch_bend(uint8_t chan, int16_t value)
{
    xprintf("PitchBend %2d  %d\n", chan, value);
}
