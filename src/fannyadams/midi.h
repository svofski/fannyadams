#pragma once

#include <inttypes.h>

typedef int8_t midi_note_t;
typedef int8_t midi_chan_t;

typedef void (*midi_note_onoff_cb_t)(midi_chan_t,midi_note_t,uint8_t);
typedef void (*midi_chan_cb_t)(midi_chan_t);
typedef void (*midi_pitchbend_cb_t)(midi_chan_t, int16_t value);

extern midi_note_onoff_cb_t midi_note_on_cb;
extern midi_note_onoff_cb_t midi_note_off_cb;
extern midi_chan_cb_t       midi_all_sound_off_cb;
extern midi_chan_cb_t       midi_all_notes_off_cb;
extern midi_chan_cb_t       midi_reset_all_cntrls_cb;
extern midi_pitchbend_cb_t  midi_pitchbend_cb;

void midi_read_usbpacket(uint32_t packet32);
