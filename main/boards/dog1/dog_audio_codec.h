#ifndef DOG_AUDIO_CODEC_H
#define DOG_AUDIO_CODEC_H

#include "codecs/no_audio_codec.h"

class DogDualMicAudioCodec : public NoAudioCodec {
public:
    DogDualMicAudioCodec(int input_sample_rate, int output_sample_rate,
                         gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                         gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din);

    int Write(const int16_t* data, int samples) override;
    int Read(int16_t* dest, int samples) override;
};

#endif // DOG_AUDIO_CODEC_H
