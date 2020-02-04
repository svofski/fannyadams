#pragma once

#include <inttypes.h>

typedef int8_t midi_note_t;

typedef void (*midi_note_onoff_cb_t)(uint8_t,midi_note_t,uint8_t);

extern midi_note_onoff_cb_t midi_note_on_cb;
extern midi_note_onoff_cb_t midi_note_off_cb;

void midi_read_usbpacket(uint32_t packet32);
