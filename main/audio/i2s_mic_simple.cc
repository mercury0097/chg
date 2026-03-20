/**
 * @file i2s_mic_simple.cc
 * @brief 简化版 I2S 麦克风处理实现
 */

#include "i2s_mic_simple.h"
#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

static const char* TAG = "I2SMicSimple";

I2SMicSimple::I2SMicSimple(int sample_rate, int frame_size)
    : rx_handle_(nullptr)
    , sample_rate_(sample_rate)
    , frame_size_(frame_size)
    , i2s_buffer_(nullptr)
    , audio_buffer_(nullptr)
    , noise_floor_(100.0f)
    , voice_threshold_(800.0f)
    , is_voice_active_(false)
    , vad_counter_(0)
    , task_handle_(nullptr)
    , running_(false)
{
    i2s_buffer_ = new int32_t[frame_size_];
    audio_buffer_ = new int16_t[frame_size_];
    ESP_LOGI(TAG, "I2SMicSimple 创建: 采样率=%d, 帧大小=%d", sample_rate_, frame_size_);
}

I2SMicSimple::~I2SMicSimple() {
    Stop();
    if (rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
    }
    delete[] i2s_buffer_;
    delete[] audio_buffer_;
}

bool I2SMicSimple::Init(int ws_pin, int sck_pin, int din_pin) {
    ESP_LOGI(TAG, "初始化 I2S: WS=%d, SCK=%d, DIN=%d", ws_pin, sck_pin, din_pin);
    
    // 创建 I2S 通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 1024;
    chan_cfg.auto_clear = true;
    
    if (i2s_new_channel(&chan_cfg, nullptr, &rx_handle_) != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S 通道失败");
        return false;
    }
    
    // 配置 I2S 标准模式
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(sample_rate_)),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(sck_pin),
            .ws = static_cast<gpio_num_t>(ws_pin),
            .dout = I2S_GPIO_UNUSED,
            .din = static_cast<gpio_num_t>(din_pin),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    
    if (i2s_channel_init_std_mode(rx_handle_, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 失败");
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
        return false;
    }
    
    if (i2s_channel_enable(rx_handle_) != ESP_OK) {
        ESP_LOGE(TAG, "启用 I2S 失败");
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
        return false;
    }
    
    ESP_LOGI(TAG, "✓ I2S 初始化成功");
    return true;
}

void I2SMicSimple::Start() {
    if (running_ || !rx_handle_) {
        return;
    }
    
    running_ = true;
    xTaskCreatePinnedToCore(
        TaskEntry,
        "i2s_mic_simple",
        4096,
        this,
        5,
        &task_handle_,
        1
    );
    ESP_LOGI(TAG, "✓ 音频任务已启动");
}

void I2SMicSimple::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (task_handle_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        task_handle_ = nullptr;
    }
    ESP_LOGI(TAG, "✓ 音频任务已停止");
}

void I2SMicSimple::SetThresholds(float noise_threshold, float voice_threshold) {
    noise_floor_ = noise_threshold;
    voice_threshold_ = voice_threshold;
    ESP_LOGI(TAG, "阈值更新: 噪声=%.0f, 语音=%.0f", noise_threshold, voice_threshold);
}

void I2SMicSimple::TaskEntry(void* arg) {
    auto* self = static_cast<I2SMicSimple*>(arg);
    self->ProcessLoop();
    vTaskDelete(nullptr);
}

void I2SMicSimple::ProcessLoop() {
    ESP_LOGI(TAG, "音频处理循环开始");
    
    int frames_per_second = sample_rate_ / frame_size_;
    int frame_count = 0;
    bool last_vad_state = false;
    
    while (running_) {
        // 读取 I2S 数据
        size_t bytes_read = 0;
        size_t bytes_to_read = frame_size_ * sizeof(int32_t);
        
        esp_err_t ret = i2s_channel_read(
            rx_handle_,
            i2s_buffer_,
            bytes_to_read,
            &bytes_read,
            portMAX_DELAY
        );
        
        if (ret != ESP_OK || bytes_read != bytes_to_read) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // 转换为 16-bit PCM
        for (int i = 0; i < frame_size_; i++) {
            audio_buffer_[i] = static_cast<int16_t>(i2s_buffer_[i] >> 16);
        }
        
        // 处理音频（降噪 + VAD）
        ProcessAudio();
        
        // VAD 状态变化回调
        if (is_voice_active_ != last_vad_state) {
            last_vad_state = is_voice_active_;
            if (vad_callback_) {
                vad_callback_(is_voice_active_);
            }
        }
        
        // 每秒输出一次 VAD 状态
        if (++frame_count >= frames_per_second) {
            frame_count = 0;
            if (is_voice_active_) {
                ESP_LOGI(TAG, "🎤 Voice detected");
            } else {
                ESP_LOGI(TAG, "… Silence");
            }
        }
        
        // 音频数据回调
        if (audio_callback_) {
            audio_callback_(audio_buffer_, frame_size_);
        }
    }
    
    ESP_LOGI(TAG, "音频处理循环结束");
}

void I2SMicSimple::ProcessAudio() {
    // 计算 RMS 能量
    float rms = CalculateRMS(audio_buffer_, frame_size_);
    
    // 更新噪声基准（使用指数移动平均）
    if (rms < noise_floor_ * 1.5f) {
        noise_floor_ = 0.95f * noise_floor_ + 0.05f * rms;
        // 限制范围
        if (noise_floor_ < 50.0f) noise_floor_ = 50.0f;
        if (noise_floor_ > 300.0f) noise_floor_ = 300.0f;
    }
    
    // 简单的能量门限降噪
    if (rms < noise_floor_ * 2.0f) {
        // 噪声：衰减到 10%
        for (int i = 0; i < frame_size_; i++) {
            audio_buffer_[i] = static_cast<int16_t>(audio_buffer_[i] * 0.1f);
        }
    }
    
    // VAD 检测（基于能量阈值）
    if (rms > voice_threshold_) {
        vad_counter_ = std::min(vad_counter_ + 1, 5);
    } else {
        vad_counter_ = std::max(vad_counter_ - 1, 0);
    }
    
    is_voice_active_ = (vad_counter_ >= 2);
}

float I2SMicSimple::CalculateRMS(const int16_t* data, size_t size) {
    if (size == 0) return 0.0f;
    
    float sum = 0.0f;
    for (size_t i = 0; i < size; i++) {
        float sample = static_cast<float>(data[i]);
        sum += sample * sample;
    }
    
    return std::sqrt(sum / size);
}

