#include <inttypes.h>
#include <stdlib.h>
#include "audiobuf.h"
#include "i2s.h"
#include "usbcmp_descriptors.h"
#include "xprintf.h"
#include "usbcmp_ac.h"
#include "util.h"

// ample sink buffer
static uint8_t audio_sink_buffer[192 * 8];
static volatile uint32_t rxhead, rxtail, rxtop = sizeof(audio_sink_buffer);


//static uint16_t mic_sample_ofs;
//static uint8_t mic_sample[480];
//
//static uint8_t audio_source_buffer[AUDIO_SOURCE_PACKET_SIZE];

void asink_init()
{
    rxtop = sizeof(audio_sink_buffer);
    rxhead = rxtail = 0;
}

size_t asink_vacant()
{
    return sizeof(audio_sink_buffer) - rxhead;
}

uint8_t * asink_head()
{
    return audio_sink_buffer + rxhead;
}

void asink_advance_head(int len)
{
    rxhead += len;
    if (rxhead >= sizeof audio_sink_buffer) {
        rxhead -= sizeof audio_sink_buffer;
    }
}

size_t asink_fullness()
{
    return (rxhead >= rxtail) ? (rxhead - rxtail) : (rxhead + rxtop - rxtail);
}

size_t asink_size()
{
    return sizeof audio_sink_buffer;
}

// Called every USB frame
// copy data from usb sink buffer to i2s pingpong buffers
void audio_data_process(void) 
{
    size_t avail = asink_fullness();

    if (avail < AUDIO_SINK_PACKET_SIZE * 2) {
        return;
    }

    int32_t* i2s_buf = I2S_GetBuffer();

    if (avail < 192) {
//            xprintf("-%d %d %d %d;\n", avail, rxhead, rxtail, rxtop);
    } if (avail > sizeof(audio_sink_buffer) - 192) {
//            xprintf("+%d %d %d %d;\n", avail, rxhead, rxtail, rxtop);
        rxtail += 192;
        if (rxtail >= rxtop) {
            rxtail -= rxtop;
        }
    } else {
    }

    if (AudioParams.Mute) {
        for (size_t i = 0; i < MIN(192,avail)/2; ++i) {
            rxtail += 2;
            if (rxtail >= rxtop) {
                rxtail = 0;
            }

            i2s_buf[i] = 0;
        }
    }
    else {
        for (size_t i = 0; i < MIN(48*2*2,avail)/2; i++) {
            uint32_t samp16 = ((uint16_t*)audio_sink_buffer)[rxtail/2];

            /* volume adjust */
            int16_t sampsign = samp16;
            sampsign /= 2;
            samp16 = sampsign;

            rxtail += 2;
            if (rxtail >= rxtop) {
                rxtail = 0;
            }

            i2s_buf[i] += samp16;
        }
    }
}
