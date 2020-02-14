/* based on amsynth by Nick Dowell */

#include <cstddef>
#include <iostream>
#include <alsa/asoundlib.h>

#include "alsaaudio.h"

const char * alsa_audio_device = "default";

int ALSAAudioDriver::write(float* buffer, int nsamples)
{
    if (!_handle) {
        return -1;
    }

    assert(nsamples <= kMaxWriteFrames);
    for (int i = 0; i < nsamples; i++) {
        short s16 = buffer[i] * 32767;
        ((unsigned char*)_buffer)[i * 2 + 0] = (s16 & 0xff);
        ((unsigned char*)_buffer)[i * 2 + 1] = ((s16 >> 8) & 0xff);
    }

    snd_pcm_sframes_t err =
      snd_pcm_writei(_handle, _buffer, nsamples / _channels);
    if (err < 0) {
        err = snd_pcm_recover(_handle, err, 1);
    }
    if (err < 0) {
        return -1;
    }
    return 0;
}

int ALSAAudioDriver::open()
{
    if (_handle != NULL) {
        return 0;
    }

#define ALSA_CALL(expr)                                                        \
    if ((err = (expr)) < 0) {                                                  \
        std::cerr << #expr << " failed with error: " << snd_strerror(err)      \
                  << std::endl;                                                \
        if (pcm) {                                                             \
            snd_pcm_close(pcm);                                                \
        }                                                                      \
        return -1;                                                             \
    }

    int err = 0;

    snd_pcm_t* pcm = NULL;
    ALSA_CALL(snd_pcm_open(&pcm, alsa_audio_device, SND_PCM_STREAM_PLAYBACK, 0));

    unsigned int latency = 40 * 1000;
    ALSA_CALL(snd_pcm_set_params(pcm,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 2,
                                 48000,
                                 0,
                                 latency));

    _handle = pcm;
    _channels = 2;
    _buffer = (short*)malloc(kMaxWriteFrames * _channels * sizeof(short));

    return 0;
}

void ALSAAudioDriver::close()
{
    if (_handle != NULL) {
        snd_pcm_close((snd_pcm_t*)_handle);
        _handle = NULL;
    }

    free(_buffer);
    _buffer = NULL;
}
