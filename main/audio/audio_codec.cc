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
  constexpr int kDefaultVolume = 70;
  constexpr int kMinimumVolume = 10;

  Settings settings("audio", true);
  output_volume_ = settings.GetInt("output_volume", output_volume_);

  // Earlier builds forced 100 into NVS on every boot. Migrate that once so
  // existing devices return to the normal playback level after upgrade.
  if (!settings.GetBool("ov_migrated", false)) {
    if (output_volume_ >= 100) {
      output_volume_ = kDefaultVolume;
      settings.SetInt("output_volume", output_volume_);
      ESP_LOGI(TAG, "Migrated forced maximum volume to %d", output_volume_);
    }
    settings.SetBool("ov_migrated", true);
  }

  if (output_volume_ <= 0) {
    ESP_LOGW(TAG,
             "Output volume value (%d) is too small, setting to minimum (%d)",
             output_volume_, kMinimumVolume);
    output_volume_ = kMinimumVolume;
    settings.SetInt("output_volume", output_volume_);
  } else if (output_volume_ > 100) {
    ESP_LOGW(TAG,
             "Output volume value (%d) is invalid, setting to default (%d)",
             output_volume_, kDefaultVolume);
    output_volume_ = kDefaultVolume;
    settings.SetInt("output_volume", output_volume_);
  }

  if (tx_handle_ != nullptr) {
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
  }

  if (rx_handle_ != nullptr) {
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
  }

  ESP_LOGI(TAG, "Audio codec started with volume %d", output_volume_);
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
