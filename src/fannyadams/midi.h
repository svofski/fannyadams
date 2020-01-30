#pragma once

typedef void (*midi_note_onoff_cb_t)(uint8_t,uint8_t,uint8_t);

extern midi_note_onoff_cb_t midi_note_on_cb;
extern midi_note_onoff_cb_t midi_note_off_cb;

void midi_read_usbpacket(uint32_t packet32);
