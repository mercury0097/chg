/**
 * @file i2s_mic_processor.h
 * @brief I2S 麦克风音频采集与降噪处理类
 * 
 * 功能：
 * - 从 ICS-43434 I2S 麦克风采集音频（16kHz，单声道）
 * - 使用 SpeexDSP 进行降噪和语音活动检测（VAD）
 * - 支持简单的能量门限降噪
 * 
 * 环境：ESP-IDF 5.5 + ESP32-S3
 */

#pragma once

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <speex/speex_preprocess.h>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <functional>

class I2SMicProcessor {
public:
    /**
     * @brief 构造函数
     * @param i2s_port I2S 端口号（默认 I2S_NUM_0）
     * @param sample_rate 采样率（默认 16000 Hz）
     * @param frame_size 每帧采样数（默认 160，即 10ms @ 16kHz）
     */
    I2SMicProcessor(i2s_port_t i2s_port = I2S_NUM_0, 
                    int sample_rate = 16000, 
                    int frame_size = 160);
    
    ~I2SMicProcessor();

    /**
     * @brief 初始化 I2S 接口
     * @param ws_pin I2S WS（LRCK）引脚
     * @param sck_pin I2S SCK（BCLK）引脚
     * @param din_pin I2S DIN（DATA）引脚
     * @return ESP_OK 表示成功
     */
    esp_err_t InitI2S(gpio_num_t ws_pin, gpio_num_t sck_pin, gpio_num_t din_pin);

    /**
     * @brief 初始化 SpeexDSP 降噪和 VAD
     * @param noise_suppress 降噪级别（-15 到 0 dB，推荐 -15）
     * @param vad_prob_start VAD 启动概率阈值（0-100，推荐 90）
     * @return true 表示成功
     */
    bool InitSpeexDSP(int noise_suppress = -15, int vad_prob_start = 90);

    /**
     * @brief 启动音频处理任务
     * @param task_priority 任务优先级（默认 5）
     */
    void Start(int task_priority = 5);

    /**
     * @brief 停止音频处理任务
     */
    void Stop();

    /**
     * @brief 设置处理后音频数据的回调函数
     * @param callback 回调函数，参数为处理后的音频帧（int16_t 数组）
     */
    void SetAudioCallback(std::function<void(const int16_t*, size_t)> callback);

    /**
     * @brief 设置 VAD 状态变化回调
     * @param callback 回调函数，参数为是否检测到语音
     */
    void SetVadCallback(std::function<void(bool)> callback);

private:
    // I2S 配置参数
    i2s_port_t i2s_port_;
    i2s_chan_handle_t rx_handle_;
    int sample_rate_;
    int frame_size_;

    // SpeexDSP 相关
    SpeexPreprocessState* speex_state_;
    int speex_frame_size_;

    // 音频缓冲区
    std::vector<int32_t> i2s_buffer_;      // I2S 原始数据（32-bit）
    std::vector<int16_t> audio_buffer_;    // 转换后的音频数据（16-bit）
    std::vector<int16_t> processed_buffer_; // 处理后的音频数据

    // 降噪相关
    float noise_floor_;                     // 噪声基准（能量门限）
    int noise_update_counter_;              // 噪声更新计数器
    static constexpr int NOISE_UPDATE_FRAMES = 50; // 每 50 帧更新一次噪声估计

    // VAD 相关
    bool is_voice_active_;                  // 当前是否检测到语音
    int vad_frame_counter_;                 // VAD 帧计数器（用于每秒输出）
    int frames_per_second_;                 // 每秒帧数

    // 任务相关
    TaskHandle_t task_handle_;
    bool running_;

    // 回调函数
    std::function<void(const int16_t*, size_t)> audio_callback_;
    std::function<void(bool)> vad_callback_;

    /**
     * @brief 音频处理任务（静态入口）
     */
    static void AudioTaskEntry(void* arg);

    /**
     * @brief 音频处理主循环
     */
    void AudioProcessingLoop();

    /**
     * @brief 从 I2S 读取一帧音频
     * @return true 表示成功读取
     */
    bool ReadFrame();

    /**
     * @brief 将 32-bit I2S 数据转换为 16-bit PCM
     */
    void ConvertI2SToInt16();

    /**
     * @brief 使用简单能量门限法进行降噪预处理
     */
    void ApplyEnergyGating();

    /**
     * @brief 使用 SpeexDSP 进行降噪和 VAD 检测
     * @return true 表示检测到语音
     */
    bool ProcessWithSpeex();

    /**
     * @brief 计算音频帧的 RMS 能量
     * @param data 音频数据
     * @param size 数据长度
     * @return RMS 能量值
     */
    float CalculateRMS(const int16_t* data, size_t size);

    /**
     * @brief 更新噪声基准（使用静音帧的平均能量）
     */
    void UpdateNoiseFloor();
};

