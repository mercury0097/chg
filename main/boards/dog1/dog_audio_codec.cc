#include "dog_audio_codec.h"

#include <cmath>

#include <esp_log.h>

#include "config.h"

#define TAG "DogAudioCodec"

namespace {
#if DOG1_SINGLE_MIC_TEST_USE_LEFT
constexpr i2s_std_slot_mask_t kDogMicSlotMask = I2S_STD_SLOT_LEFT;
constexpr const char* kDogMicSlotLabel = "LEFT";
#else
constexpr i2s_std_slot_mask_t kDogMicSlotMask = I2S_STD_SLOT_RIGHT;
constexpr const char* kDogMicSlotLabel = "RIGHT";
#endif
}  // namespace

DogDualMicAudioCodec::DogDualMicAudioCodec(int input_sample_rate, int output_sample_rate,
                                           gpio_num_t spk_bclk, gpio_num_t spk_ws,
                                           gpio_num_t spk_dout, gpio_num_t mic_sck,
                                           gpio_num_t mic_ws, gpio_num_t mic_din) {
    duplex_ = false;
    input_reference_ = false;
    input_channels_ = AUDIO_INPUT_MICROPHONE_CHANNELS;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    i2s_chan_config_t chan_cfg = {
        .id = static_cast<i2s_port_t>(0),
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = static_cast<uint32_t>(output_sample_rate_),
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    chan_cfg.id = static_cast<i2s_port_t>(1);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));
    std_cfg.clk_cfg.sample_rate_hz = static_cast<uint32_t>(input_sample_rate_);
    std_cfg.slot_cfg.slot_mode =
        input_channels_ > 1 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask =
        input_channels_ > 1 ? I2S_STD_SLOT_BOTH : kDogMicSlotMask;
    std_cfg.gpio_cfg.bclk = mic_sck;
    std_cfg.gpio_cfg.ws = mic_ws;
    std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.din = mic_din;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));

    ESP_LOGI(TAG,
             "Dog dual-mic codec created (input_channels=%d, microphone_channels=%d, input_reference=%d)",
             input_channels_, microphone_channels(), input_reference_);
    if (input_channels_ > 1) {
        ESP_LOGI(TAG,
                 "DOG1 mic topology: shared I2S data line with stereo slots (LEFT + RIGHT)");
        ESP_LOGI(TAG,
                 "DOG1 channel layout: frame=[mic_left, mic_right], mic_channels=%d, ref_channels=%d, total_input_channels=%d",
                 microphone_channels(), input_reference_ ? 1 : 0, input_channels_);
    } else {
        ESP_LOGW(TAG,
                 "DOG1 single-mic test mode enabled: reading only %s slot to isolate hardware issues",
                 kDogMicSlotLabel);
    }
    ESP_LOGI(TAG,
             "DOG1 I2S pins: mic_bclk=%d mic_ws=%d mic_din=%d spk_bclk=%d spk_ws=%d spk_dout=%d",
             mic_sck, mic_ws, mic_din, spk_bclk, spk_ws, spk_dout);
    ESP_LOGI(TAG,
             "DOG1 hardware expectation: one ICS-43434 strapped LEFT (LR=0), one strapped RIGHT (LR=1) on the same SD bus");
}

int DogDualMicAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> data_lock(data_if_mutex_);
    std::vector<int32_t> buffer(samples);

    double volume_scale = static_cast<double>(output_volume_) / 100.0;
    if (volume_scale > 0.0) {
        volume_scale = pow(volume_scale, 2.0);
        volume_scale *= 0.8;
    }

    const double max_val = static_cast<double>(INT32_MAX);
    const double min_val = static_cast<double>(INT32_MIN);

    for (int i = 0; i < samples; ++i) {
        int32_t sample_32 = static_cast<int32_t>(data[i]) << 16;
        double scaled = static_cast<double>(sample_32) * volume_scale;

        if (scaled > max_val) {
            buffer[i] = INT32_MAX;
        } else if (scaled < min_val) {
            buffer[i] = INT32_MIN;
        } else {
            buffer[i] = static_cast<int32_t>(scaled);
        }
    }

    size_t bytes_written = 0;
    ESP_ERROR_CHECK(i2s_channel_write(tx_handle_, buffer.data(),
                                      samples * sizeof(int32_t), &bytes_written,
                                      portMAX_DELAY));
    return bytes_written / sizeof(int32_t);
}

int DogDualMicAudioCodec::Read(int16_t* dest, int samples) {
    std::vector<int32_t> buffer(samples);

    size_t bytes_read = 0;
    if (i2s_channel_read(rx_handle_, buffer.data(),
                         buffer.size() * sizeof(int32_t), &bytes_read,
                         portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Read failed");
        return 0;
    }

    const int actual_samples = bytes_read / sizeof(int32_t);
    for (int i = 0; i < actual_samples; ++i) {
        const int32_t value = buffer[i] >> 12;
        dest[i] = (value > INT16_MAX)
                      ? INT16_MAX
                      : (value < -INT16_MAX ? -INT16_MAX
                                            : static_cast<int16_t>(value));
    }

    return actual_samples;
}
