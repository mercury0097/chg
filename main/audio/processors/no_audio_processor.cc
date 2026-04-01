#include "no_audio_processor.h"
#include <esp_log.h>

#define TAG "NoAudioProcessor"

namespace {
std::vector<int16_t> MixMicrophoneChannelsToMono(const std::vector<int16_t> &data,
                                                 int input_channels,
                                                 int microphone_channels) {
    auto mono_data = std::vector<int16_t>(data.size() / input_channels);
    for (size_t frame = 0, index = 0; frame < mono_data.size(); ++frame, index += input_channels) {
        int32_t sample_sum = 0;
        for (int channel = 0; channel < microphone_channels; ++channel) {
            sample_sum += data[index + channel];
        }
        mono_data[frame] = static_cast<int16_t>(sample_sum / microphone_channels);
    }
    return mono_data;
}
} // namespace

void NoAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;
}

void NoAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (!is_running_ || !output_callback_) {
        return;
    }

    if (codec_->input_channels() > 1) {
        output_callback_(MixMicrophoneChannelsToMono(
            data, codec_->input_channels(), codec_->microphone_channels()));
        return;
    }

    output_callback_(std::move(data));
}

void NoAudioProcessor::Start() {
    is_running_ = true;
}

void NoAudioProcessor::Stop() {
    is_running_ = false;
}

bool NoAudioProcessor::IsRunning() {
    return is_running_;
}

void NoAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void NoAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

size_t NoAudioProcessor::GetFeedSize() {
    if (!codec_) {
        return 0;
    }
    return frame_samples_;
}

void NoAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
        ESP_LOGE(TAG, "Device AEC is not supported");
    }
}
