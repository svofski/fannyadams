#include <stdio.h>

#include "midi.h"

midi_note_onoff_cb_t midi_note_on_cb;
midi_note_onoff_cb_t midi_note_off_cb;

int main()
{
    extern int event_test();
    extern int adsr_test();
    extern int osc_test();

    int res = 0;
    
    res |= event_test();
    res |= adsr_test();
    res |= osc_test();
    
    return res;
}
