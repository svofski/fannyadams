#pragma once

#include <alsa/asoundlib.h>

class ALSAAudioDriver
{
    enum
    {
        kMaxWriteFrames = 512
    };

  public:
    ALSAAudioDriver()
      : _handle(0)
      , _buffer(0)
      , _channels(0)
    {}

    virtual int open();
    virtual void close();
    virtual int write(float* buffer, int frames);

  private:
    snd_pcm_t* _handle;
    short* _buffer;
    unsigned _channels;
};
