/**
 * @file i2s_mic_test.cc
 * @brief I2S 麦克风测试程序
 * 
 * 使用说明：
 * 1. 在 main.cc 的 app_main() 中调用 i2s_mic_test_start()
 * 2. 或者取消下面的 #define I2S_MIC_AUTO_START 让它自动启动
 */

#include "i2s_mic_simple.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>
#include <cstdint>

static const char* TAG = "I2SMicTest";

// ==================== 配置区域 ====================
// 根据您的硬件修改这些引脚
#define I2S_WS_PIN      42    // Word Select
#define I2S_SCK_PIN     41    // Serial Clock
#define I2S_DIN_PIN     2     // Data In

// 如果要自动启动，取消下面的注释
// #define I2S_MIC_AUTO_START

// ==================== 全局变量 ====================
static I2SMicSimple* g_mic = nullptr;

// ==================== 回调函数 ====================

/**
 * @brief 音频数据回调
 */
void on_audio_data(const int16_t* data, size_t size) {
    // 这里可以处理音频数据
    // 例如：发送到语音识别引擎
    
    // 示例：每 100 帧打印一次平均值
    static int count = 0;
    if (++count >= 100) {
        count = 0;
        int32_t sum = 0;
        for (size_t i = 0; i < size; i++) {
            sum += data[i];
        }
        int16_t avg = sum / size;
        ESP_LOGD(TAG, "音频平均值: %d", avg);
    }
}

/**
 * @brief VAD 状态变化回调
 */
void on_vad_change(bool is_voice) {
    if (is_voice) {
        ESP_LOGI(TAG, ">>> 检测到语音 <<<");
    } else {
        ESP_LOGI(TAG, "<<< 恢复静音 >>>");
    }
}

// ==================== 初始化函数 ====================

/**
 * @brief 初始化并启动 I2S 麦克风
 * 
 * 在您的 app_main() 中调用此函数即可启动麦克风降噪功能
 */
extern "C" void i2s_mic_test_start(void) {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  I2S 麦克风降噪测试");
    ESP_LOGI(TAG, "  采样率: 16000 Hz");
    ESP_LOGI(TAG, "  引脚: WS=%d, SCK=%d, DIN=%d", I2S_WS_PIN, I2S_SCK_PIN, I2S_DIN_PIN);
    ESP_LOGI(TAG, "==========================================");
    
    // 创建麦克风实例
    g_mic = new I2SMicSimple(16000, 160);
    
    // 初始化 I2S
    if (!g_mic->Init(I2S_WS_PIN, I2S_SCK_PIN, I2S_DIN_PIN)) {
        ESP_LOGE(TAG, "❌ I2S 初始化失败");
        delete g_mic;
        g_mic = nullptr;
        return;
    }
    
    // 设置降噪和 VAD 阈值
    // 参数：噪声阈值（默认 500），语音阈值（默认 800）
    // 如果麦克风太灵敏，增大这些值；如果不够灵敏，减小这些值
    g_mic->SetThresholds(300.0f, 800.0f);
    
    // 设置回调函数
    g_mic->SetAudioCallback(on_audio_data);
    g_mic->SetVadCallback(on_vad_change);
    
    // 启动
    g_mic->Start();
    
    ESP_LOGI(TAG, "✓ I2S 麦克风已启动");
    ESP_LOGI(TAG, "正在监听音频... 每 1 秒输出一次 VAD 状态");
}

/**
 * @brief 停止 I2S 麦克风
 */
extern "C" void i2s_mic_test_stop(void) {
    if (g_mic) {
        g_mic->Stop();
        delete g_mic;
        g_mic = nullptr;
        ESP_LOGI(TAG, "✓ I2S 麦克风已停止");
    }
}

// ==================== 自动启动（可选）====================

#ifdef I2S_MIC_AUTO_START

// 如果定义了 I2S_MIC_AUTO_START，则自动创建 app_main
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "自动启动 I2S 麦克风测试...");
    
    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 启动麦克风
    i2s_mic_test_start();
    
    // 主循环
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 这里可以添加其他应用逻辑
    }
}

#endif // I2S_MIC_AUTO_START

