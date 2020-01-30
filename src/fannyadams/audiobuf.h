#pragma once

#include <inttypes.h>
#include <stdlib.h>

void asink_init(void);

size_t asink_vacant(void);          // cound of bytes until the end
uint8_t * asink_head(void);         // pointer to buffer head
void asink_advance_head(int len);   // advance head after filling

size_t asink_fullness(void);
size_t asink_size(void);

void audio_data_process(void);
void asink_dma_cb(void);
