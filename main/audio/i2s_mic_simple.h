/**
 * @file i2s_mic_simple.h
 * @brief 简化版 I2S 麦克风处理（无需 SpeexDSP，可直接编译）
 * 
 * 功能：
 * - I2S 麦克风音频采集（ICS-43434）
 * - 简单能量门限降噪
 * - 基于能量的 VAD 检测
 * 
 * 使用：
 *   I2SMicSimple mic;
 *   mic.Init(GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_2);
 *   mic.Start();
 */

#pragma once

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdint>
#include <cstdlib>
#include <functional>

class I2SMicSimple {
public:
    I2SMicSimple(int sample_rate = 16000, int frame_size = 160);
    ~I2SMicSimple();

    /**
     * @brief 初始化 I2S
     * @param ws_pin WS 引脚
     * @param sck_pin SCK 引脚
     * @param din_pin DIN 引脚
     */
    bool Init(int ws_pin, int sck_pin, int din_pin);

    /**
     * @brief 启动音频处理
     */
    void Start();

    /**
     * @brief 停止音频处理
     */
    void Stop();

    /**
     * @brief 设置音频数据回调
     */
    void SetAudioCallback(std::function<void(const int16_t*, size_t)> callback) {
        audio_callback_ = callback;
    }

    /**
     * @brief 设置 VAD 回调
     */
    void SetVadCallback(std::function<void(bool)> callback) {
        vad_callback_ = callback;
    }

    /**
     * @brief 设置降噪和 VAD 参数
     * @param noise_threshold 噪声阈值（默认 500）
     * @param voice_threshold 语音阈值（默认 1000）
     */
    void SetThresholds(float noise_threshold, float voice_threshold);

private:
    i2s_chan_handle_t rx_handle_;
    int sample_rate_;
    int frame_size_;
    
    int32_t* i2s_buffer_;
    int16_t* audio_buffer_;
    
    float noise_floor_;
    float voice_threshold_;
    bool is_voice_active_;
    int vad_counter_;
    
    TaskHandle_t task_handle_;
    bool running_;
    
    std::function<void(const int16_t*, size_t)> audio_callback_;
    std::function<void(bool)> vad_callback_;
    
    static void TaskEntry(void* arg);
    void ProcessLoop();
    float CalculateRMS(const int16_t* data, size_t size);
    void ProcessAudio();
};

