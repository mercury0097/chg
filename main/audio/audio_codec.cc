#include "audio_codec.h"
#include "board.h"
#include "settings.h"

#include <cstring>
#include <driver/i2s_common.h>
#include <esp_log.h>

#define TAG "AudioCodec"

AudioCodec::AudioCodec() {}

AudioCodec::~AudioCodec() {}

void AudioCodec::OutputData(std::vector<int16_t> &data) {
  Write(data.data(), data.size());
}

bool AudioCodec::InputData(std::vector<int16_t> &data) {
  int samples = Read(data.data(), data.size());
  if (samples > 0) {
    return true;
  }
  return false;
}

void AudioCodec::Start() {
  constexpr int kDefaultVolume = 100; // 开机默认最大音量

  // 强制使用最大音量,不读取 NVS 中的旧值
  output_volume_ = kDefaultVolume;
  ESP_LOGI(TAG, "Using default maximum volume: %d", output_volume_);

  // 保存到 NVS,下次开机继续使用最大音量
  Settings settings_rw("audio", true);
  settings_rw.SetInt("output_volume", output_volume_);
  ESP_LOGI(TAG, "Saved volume to NVS: %d", output_volume_);

  if (tx_handle_ != nullptr) {
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
  }

  if (rx_handle_ != nullptr) {
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
  }

  EnableInput(true);
  EnableOutput(true);
  ESP_LOGI(TAG, "Audio codec started");
}

void AudioCodec::SetOutputVolume(int volume) {
  output_volume_ = volume;
  ESP_LOGI(TAG, "Set output volume to %d", output_volume_);

  Settings settings("audio", true);
  settings.SetInt("output_volume", output_volume_);
}

void AudioCodec::SetInputGain(float gain) {
  input_gain_ = gain;
  ESP_LOGI(TAG, "Set input gain to %.1f", input_gain_);
}

void AudioCodec::EnableInput(bool enable) {
  if (enable == input_enabled_) {
    return;
  }
  input_enabled_ = enable;
  ESP_LOGI(TAG, "Set input enable to %s", enable ? "true" : "false");
}

void AudioCodec::EnableOutput(bool enable) {
  if (enable == output_enabled_) {
    return;
  }
  output_enabled_ = enable;
  ESP_LOGI(TAG, "Set output enable to %s", enable ? "true" : "false");
}
