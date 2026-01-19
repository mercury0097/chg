/**
 * @file i2s_mic_example.cc
 * @brief I2S 麦克风降噪处理完整示例
 * 
 * 使用说明：
 * 1. 在 menuconfig 中启用 SpeexDSP 组件
 * 2. 配置 I2S 引脚（根据实际硬件修改）
 * 3. 编译并烧录到 ESP32-S3
 * 4. 打开串口监视器查看 VAD 检测结果
 * 
 * 示例硬件连接（ESP32-S3 + ICS-43434）：
 * - ICS-43434 WS   -> GPIO 42
 * - ICS-43434 SCK  -> GPIO 41
 * - ICS-43434 SD   -> GPIO 2
 * - ICS-43434 VDD  -> 3.3V
 * - ICS-43434 GND  -> GND
 */

#include "i2s_mic_processor.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>

static const char* TAG = "I2SMicExample";

// ==================== I2S 引脚配置 ====================
// 根据您的硬件修改这些引脚定义
#define I2S_WS_PIN      GPIO_NUM_42    // Word Select (LRCK)
#define I2S_SCK_PIN     GPIO_NUM_41    // Serial Clock (BCLK)
#define I2S_DIN_PIN     GPIO_NUM_2     // Serial Data In (DATA)

// ==================== 全局变量 ====================
static I2SMicProcessor* mic_processor = nullptr;

// ==================== 回调函数 ====================

/**
 * @brief 处理后的音频数据回调
 * 在这里可以进一步处理音频数据，例如：
 * - 发送到语音识别引擎
 * - 保存到 SD 卡
 * - 通过网络发送
 */
void OnAudioData(const int16_t* data, size_t size) {
    // 示例：计算音频能量（可选）
    float energy = 0.0f;
    for (size_t i = 0; i < size; i++) {
        energy += static_cast<float>(data[i]) * data[i];
    }
    energy = std::sqrt(energy / size);
    
    // 每 100 帧输出一次能量值
    static int counter = 0;
    if (++counter >= 100) {
        counter = 0;
        ESP_LOGD(TAG, "音频能量: %.2f", energy);
    }
}

/**
 * @brief VAD 状态变化回调
 * 当检测到语音开始或结束时触发
 */
void OnVadStateChange(bool is_voice) {
    if (is_voice) {
        ESP_LOGI(TAG, ">>> 语音开始 <<<");
    } else {
        ESP_LOGI(TAG, "<<< 语音结束 >>>");
    }
}

// ==================== 初始化函数 ====================

/**
 * @brief 初始化并启动 I2S 麦克风处理器
 */
bool InitializeI2SMic() {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  ESP32 I2S 麦克风降噪示例");
    ESP_LOGI(TAG, "  采样率: 16000 Hz");
    ESP_LOGI(TAG, "  通道: 单声道");
    ESP_LOGI(TAG, "  麦克风: ICS-43434");
    ESP_LOGI(TAG, "==========================================");
    
    // 1. 创建 I2SMicProcessor 实例
    // 参数：I2S 端口, 采样率, 帧大小（160 采样 = 10ms @ 16kHz）
    mic_processor = new I2SMicProcessor(I2S_NUM_0, 16000, 160);
    if (!mic_processor) {
        ESP_LOGE(TAG, "创建 I2SMicProcessor 失败");
        return false;
    }
    
    // 2. 初始化 I2S 接口
    esp_err_t ret = mic_processor->InitI2S(I2S_WS_PIN, I2S_SCK_PIN, I2S_DIN_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 失败");
        delete mic_processor;
        mic_processor = nullptr;
        return false;
    }
    
    // 3. 初始化 SpeexDSP
    // 参数：降噪级别（-15 dB）, VAD 启动阈值（90%）
    if (!mic_processor->InitSpeexDSP(-15, 90)) {
        ESP_LOGE(TAG, "初始化 SpeexDSP 失败");
        delete mic_processor;
        mic_processor = nullptr;
        return false;
    }
    
    // 4. 设置回调函数
    mic_processor->SetAudioCallback(OnAudioData);
    mic_processor->SetVadCallback(OnVadStateChange);
    
    // 5. 启动音频处理任务
    mic_processor->Start(5);  // 优先级 5
    
    ESP_LOGI(TAG, "✓ I2S 麦克风处理器启动成功");
    ESP_LOGI(TAG, "正在监听音频... (每 1 秒输出一次 VAD 状态)");
    
    return true;
}

// ==================== 主函数 ====================

/**
 * @brief 示例主函数
 * 在您的 app_main() 中调用此函数
 */
extern "C" void i2s_mic_example_main(void) {
    // 初始化并启动 I2S 麦克风
    if (!InitializeI2SMic()) {
        ESP_LOGE(TAG, "I2S 麦克风初始化失败");
        return;
    }
    
    // 主循环（保持任务运行）
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 这里可以添加其他应用逻辑
        // 例如：检查按钮、处理网络请求等
    }
}

// ==================== 可选：独立测试程序 ====================

#ifdef I2S_MIC_STANDALONE_TEST

/**
 * @brief 独立测试程序入口
 * 如果要将此文件作为独立程序运行，取消注释 I2S_MIC_STANDALONE_TEST
 */
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "启动 I2S 麦克风降噪测试程序...");
    
    // 延迟一下，等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 运行示例
    i2s_mic_example_main();
}

#endif

// ==================== 高级用法示例 ====================

/**
 * @brief 高级示例：动态调整 SpeexDSP 参数
 * 
 * 使用场景：
 * - 根据环境噪声水平动态调整降噪强度
 * - 根据用户设置调整 VAD 灵敏度
 */
void AdjustSpeexParameters(int noise_level) {
    // 注意：此功能需要在 I2SMicProcessor 类中添加接口
    // 这里仅作为示例说明
    
    /*
    if (noise_level < 30) {
        // 低噪声环境：降低降噪强度，保留更多细节
        mic_processor->SetNoiseSuppression(-5);
    } else if (noise_level < 60) {
        // 中等噪声环境：标准降噪
        mic_processor->SetNoiseSuppression(-15);
    } else {
        // 高噪声环境：强降噪
        mic_processor->SetNoiseSuppression(-25);
    }
    */
    
    ESP_LOGI(TAG, "调整降噪参数: 噪声水平=%d", noise_level);
}

/**
 * @brief 高级示例：音频数据保存到缓冲区
 * 
 * 使用场景：
 * - 录音功能
 * - 语音唤醒后保存音频片段
 */
class AudioBuffer {
public:
    AudioBuffer(size_t capacity) : capacity_(capacity) {
        buffer_.reserve(capacity);
    }
    
    void Append(const int16_t* data, size_t size) {
        for (size_t i = 0; i < size && buffer_.size() < capacity_; i++) {
            buffer_.push_back(data[i]);
        }
    }
    
    const std::vector<int16_t>& GetBuffer() const {
        return buffer_;
    }
    
    void Clear() {
        buffer_.clear();
    }
    
    bool IsFull() const {
        return buffer_.size() >= capacity_;
    }
    
private:
    size_t capacity_;
    std::vector<int16_t> buffer_;
};

// 全局音频缓冲区（例如保存 5 秒音频）
static AudioBuffer* audio_recorder = nullptr;

void StartRecording() {
    if (!audio_recorder) {
        audio_recorder = new AudioBuffer(16000 * 5);  // 5 秒 @ 16kHz
    }
    audio_recorder->Clear();
    
    // 修改音频回调，将数据保存到缓冲区
    mic_processor->SetAudioCallback([](const int16_t* data, size_t size) {
        if (audio_recorder && !audio_recorder->IsFull()) {
            audio_recorder->Append(data, size);
        }
    });
    
    ESP_LOGI(TAG, "开始录音...");
}

void StopRecording() {
    if (audio_recorder && audio_recorder->IsFull()) {
        ESP_LOGI(TAG, "录音完成，缓冲区大小: %d 采样", 
                 audio_recorder->GetBuffer().size());
        
        // 这里可以将音频保存到文件或发送到服务器
        // SaveToFile(audio_recorder->GetBuffer());
    }
}

// ==================== 调试辅助函数 ====================

/**
 * @brief 打印系统信息
 */
void PrintSystemInfo() {
    ESP_LOGI(TAG, "========== 系统信息 ==========");
    ESP_LOGI(TAG, "芯片型号: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "CPU 频率: %d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "可用堆内存: %d 字节", esp_get_free_heap_size());
    ESP_LOGI(TAG, "可用 PSRAM: %d 字节", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "IDF 版本: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "==============================");
}

/**
 * @brief 测试 I2S 数据读取（不进行降噪处理）
 * 用于调试 I2S 配置是否正确
 */
void TestRawI2SRead() {
    ESP_LOGI(TAG, "测试原始 I2S 读取...");
    
    i2s_chan_handle_t test_rx_handle = nullptr;
    
    // 创建临时 I2S 通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &test_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建测试 I2S 通道失败");
        return;
    }
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ret = i2s_channel_init_std_mode(test_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化测试 I2S 失败");
        i2s_del_channel(test_rx_handle);
        return;
    }
    
    i2s_channel_enable(test_rx_handle);
    
    // 读取 10 帧数据
    int32_t buffer[160];
    for (int i = 0; i < 10; i++) {
        size_t bytes_read = 0;
        ret = i2s_channel_read(test_rx_handle, buffer, sizeof(buffer), &bytes_read, pdMS_TO_TICKS(1000));
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "读取 %d 字节, 第一个采样: 0x%08X", bytes_read, buffer[0]);
        } else {
            ESP_LOGE(TAG, "读取失败: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 清理
    i2s_channel_disable(test_rx_handle);
    i2s_del_channel(test_rx_handle);
    
    ESP_LOGI(TAG, "测试完成");
}

