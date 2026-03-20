/**
 * @file i2s_mic_processor.cc
 * @brief I2S 麦克风音频采集与降噪处理实现
 */

#include "i2s_mic_processor.h"
#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

static const char* TAG = "I2SMicProcessor";

// ==================== 构造与析构 ====================

I2SMicProcessor::I2SMicProcessor(i2s_port_t i2s_port, int sample_rate, int frame_size)
    : i2s_port_(i2s_port)
    , rx_handle_(nullptr)
    , sample_rate_(sample_rate)
    , frame_size_(frame_size)
    , speex_state_(nullptr)
    , speex_frame_size_(frame_size)
    , noise_floor_(100.0f)
    , noise_update_counter_(0)
    , is_voice_active_(false)
    , vad_frame_counter_(0)
    , task_handle_(nullptr)
    , running_(false)
{
    // 预分配缓冲区
    i2s_buffer_.resize(frame_size_);
    audio_buffer_.resize(frame_size_);
    processed_buffer_.resize(frame_size_);
    
    // 计算每秒帧数（用于 VAD 状态输出）
    frames_per_second_ = sample_rate_ / frame_size_;
    
    ESP_LOGI(TAG, "I2SMicProcessor 初始化: 采样率=%d Hz, 帧大小=%d, 每秒帧数=%d",
             sample_rate_, frame_size_, frames_per_second_);
}

I2SMicProcessor::~I2SMicProcessor() {
    Stop();
    
    // 释放 SpeexDSP
    if (speex_state_) {
        speex_preprocess_state_destroy(speex_state_);
        speex_state_ = nullptr;
    }
    
    // 释放 I2S
    if (rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
    }
}

// ==================== I2S 初始化 ====================

esp_err_t I2SMicProcessor::InitI2S(gpio_num_t ws_pin, gpio_num_t sck_pin, gpio_num_t din_pin) {
    ESP_LOGI(TAG, "初始化 I2S: WS=%d, SCK=%d, DIN=%d", ws_pin, sck_pin, din_pin);
    
    // 1. 创建 I2S RX 通道配置
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_port_, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;          // DMA 描述符数量
    chan_cfg.dma_frame_num = 1024;      // 每个 DMA 缓冲区的帧数
    chan_cfg.auto_clear = true;         // 自动清除 DMA 缓冲区
    
    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &rx_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 2. 配置 I2S 标准模式（ICS-43434 使用标准 I2S 格式）
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = sck_pin,
            .ws = ws_pin,
            .dout = I2S_GPIO_UNUSED,
            .din = din_pin,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    // ICS-43434 是 24-bit 有效数据，左对齐在 32-bit 中
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;  // 使用左声道
    
    ret = i2s_channel_init_std_mode(rx_handle_, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 标准模式失败: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
        return ret;
    }
    
    // 3. 启用 I2S 通道
    ret = i2s_channel_enable(rx_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启用 I2S 通道失败: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
        return ret;
    }
    
    ESP_LOGI(TAG, "✓ I2S 初始化成功");
    return ESP_OK;
}

// ==================== SpeexDSP 初始化 ====================

bool I2SMicProcessor::InitSpeexDSP(int noise_suppress, int vad_prob_start) {
    ESP_LOGI(TAG, "初始化 SpeexDSP: 降噪=%d dB, VAD阈值=%d", noise_suppress, vad_prob_start);
    
    // 创建 Speex 预处理状态
    speex_state_ = speex_preprocess_state_init(speex_frame_size_, sample_rate_);
    if (!speex_state_) {
        ESP_LOGE(TAG, "创建 SpeexPreprocessState 失败");
        return false;
    }
    
    // 配置降噪参数
    int denoise = 1;  // 启用降噪
    speex_preprocess_ctl(speex_state_, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
    speex_preprocess_ctl(speex_state_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise_suppress);
    
    // 配置 VAD 参数
    int vad = 1;  // 启用 VAD
    speex_preprocess_ctl(speex_state_, SPEEX_PREPROCESS_SET_VAD, &vad);
    speex_preprocess_ctl(speex_state_, SPEEX_PREPROCESS_SET_VAD_PROB_START, &vad_prob_start);
    
    // 可选：启用自动增益控制（AGC）
    int agc = 0;  // 暂时禁用，避免过度放大噪声
    speex_preprocess_ctl(speex_state_, SPEEX_PREPROCESS_SET_AGC, &agc);
    
    // 可选：设置回声抑制（如果不需要可以关闭）
    int dereverb = 0;  // 关闭去混响
    speex_preprocess_ctl(speex_state_, SPEEX_PREPROCESS_SET_DEREVERB, &dereverb);
    
    ESP_LOGI(TAG, "✓ SpeexDSP 初始化成功");
    return true;
}

// ==================== 任务控制 ====================

void I2SMicProcessor::Start(int task_priority) {
    if (running_) {
        ESP_LOGW(TAG, "音频处理任务已在运行");
        return;
    }
    
    if (!rx_handle_) {
        ESP_LOGE(TAG, "I2S 未初始化，无法启动任务");
        return;
    }
    
    running_ = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        AudioTaskEntry,
        "i2s_mic_task",
        4096,               // 栈大小
        this,               // 参数
        task_priority,      // 优先级
        &task_handle_,
        1                   // 固定到核心 1（音频处理通常在核心 1）
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建音频处理任务失败");
        running_ = false;
    } else {
        ESP_LOGI(TAG, "✓ 音频处理任务已启动");
    }
}

void I2SMicProcessor::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (task_handle_) {
        // 等待任务结束（最多 1 秒）
        for (int i = 0; i < 10 && eTaskGetState(task_handle_) != eDeleted; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "✓ 音频处理任务已停止");
}

// ==================== 回调设置 ====================

void I2SMicProcessor::SetAudioCallback(std::function<void(const int16_t*, size_t)> callback) {
    audio_callback_ = callback;
}

void I2SMicProcessor::SetVadCallback(std::function<void(bool)> callback) {
    vad_callback_ = callback;
}

// ==================== 音频处理主循环 ====================

void I2SMicProcessor::AudioTaskEntry(void* arg) {
    auto* processor = static_cast<I2SMicProcessor*>(arg);
    processor->AudioProcessingLoop();
    vTaskDelete(nullptr);
}

void I2SMicProcessor::AudioProcessingLoop() {
    ESP_LOGI(TAG, "音频处理循环开始");
    
    bool last_voice_state = false;
    
    while (running_) {
        // 1. 读取一帧音频数据
        if (!ReadFrame()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // 2. 转换 32-bit I2S 数据为 16-bit PCM
        ConvertI2SToInt16();
        
        // 3. 简单的能量门限预处理（可选）
        ApplyEnergyGating();
        
        // 4. 使用 SpeexDSP 进行降噪和 VAD 检测
        bool voice_detected = ProcessWithSpeex();
        
        // 5. VAD 状态变化处理
        if (voice_detected != last_voice_state) {
            last_voice_state = voice_detected;
            if (vad_callback_) {
                vad_callback_(voice_detected);
            }
        }
        
        // 6. 每秒输出一次 VAD 状态
        vad_frame_counter_++;
        if (vad_frame_counter_ >= frames_per_second_) {
            vad_frame_counter_ = 0;
            if (voice_detected) {
                ESP_LOGI(TAG, "🎤 Voice detected");
            } else {
                ESP_LOGI(TAG, "… Silence");
            }
        }
        
        // 7. 调用音频数据回调（如果需要处理后的音频）
        if (audio_callback_) {
            audio_callback_(processed_buffer_.data(), processed_buffer_.size());
        }
    }
    
    ESP_LOGI(TAG, "音频处理循环结束");
}

// ==================== I2S 数据读取 ====================

bool I2SMicProcessor::ReadFrame() {
    if (!rx_handle_) {
        return false;
    }
    
    size_t bytes_read = 0;
    size_t bytes_to_read = frame_size_ * sizeof(int32_t);
    
    esp_err_t ret = i2s_channel_read(
        rx_handle_,
        i2s_buffer_.data(),
        bytes_to_read,
        &bytes_read,
        portMAX_DELAY  // 阻塞等待
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 读取失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    if (bytes_read != bytes_to_read) {
        ESP_LOGW(TAG, "I2S 读取不完整: 期望 %d 字节, 实际 %d 字节", bytes_to_read, bytes_read);
        return false;
    }
    
    return true;
}

// ==================== 数据格式转换 ====================

void I2SMicProcessor::ConvertI2SToInt16() {
    // ICS-43434 输出 24-bit 数据，左对齐在 32-bit 中
    // 最高 8 位是符号扩展，实际数据在 bit[31:8]
    // 我们需要右移 16 位来得到 16-bit PCM（丢弃最低 8 位）
    
    for (size_t i = 0; i < frame_size_; i++) {
        // 方法 1：直接右移 16 位（保留高 16 位）
        audio_buffer_[i] = static_cast<int16_t>(i2s_buffer_[i] >> 16);
        
        // 方法 2（可选）：如果需要保留更多精度，右移 8 位然后除以 256
        // audio_buffer_[i] = static_cast<int16_t>((i2s_buffer_[i] >> 8) / 256);
    }
}

// ==================== 能量门限降噪 ====================

void I2SMicProcessor::ApplyEnergyGating() {
    // 计算当前帧的 RMS 能量
    float rms = CalculateRMS(audio_buffer_.data(), audio_buffer_.size());
    
    // 更新噪声基准（使用低能量帧）
    if (rms < noise_floor_ * 1.5f) {
        noise_update_counter_++;
        if (noise_update_counter_ >= NOISE_UPDATE_FRAMES) {
            UpdateNoiseFloor();
            noise_update_counter_ = 0;
        }
    }
    
    // 如果能量低于噪声基准的 2 倍，认为是噪声，进行衰减
    if (rms < noise_floor_ * 2.0f) {
        float attenuation = 0.1f;  // 衰减到 10%
        for (size_t i = 0; i < audio_buffer_.size(); i++) {
            audio_buffer_[i] = static_cast<int16_t>(audio_buffer_[i] * attenuation);
        }
    }
}

void I2SMicProcessor::UpdateNoiseFloor() {
    // 使用当前低能量帧的平均值更新噪声基准
    float rms = CalculateRMS(audio_buffer_.data(), audio_buffer_.size());
    
    // 使用指数移动平均（EMA）平滑更新
    float alpha = 0.1f;  // 平滑系数
    noise_floor_ = alpha * rms + (1.0f - alpha) * noise_floor_;
    
    // 限制噪声基准的范围（避免过低或过高）
    noise_floor_ = std::max(50.0f, std::min(noise_floor_, 500.0f));
}

float I2SMicProcessor::CalculateRMS(const int16_t* data, size_t size) {
    if (size == 0) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (size_t i = 0; i < size; i++) {
        float sample = static_cast<float>(data[i]);
        sum += sample * sample;
    }
    
    return std::sqrt(sum / size);
}

// ==================== SpeexDSP 处理 ====================

bool I2SMicProcessor::ProcessWithSpeex() {
    if (!speex_state_) {
        // 如果没有初始化 Speex，直接复制音频数据
        std::memcpy(processed_buffer_.data(), audio_buffer_.data(), 
                    audio_buffer_.size() * sizeof(int16_t));
        return false;
    }
    
    // 复制到处理缓冲区
    std::memcpy(processed_buffer_.data(), audio_buffer_.data(), 
                audio_buffer_.size() * sizeof(int16_t));
    
    // 运行 SpeexDSP 预处理（降噪 + VAD）
    // 返回值：1 表示检测到语音，0 表示静音
    int vad_result = speex_preprocess_run(speex_state_, processed_buffer_.data());
    
    // 更新 VAD 状态
    is_voice_active_ = (vad_result == 1);
    
    return is_voice_active_;
}

