/**
 * @file i2s_mic_full.cc
 * @brief I2S 麦克风 + ESP-SR AFE 完整版（专业降噪）
 * 
 * 功能：
 * - 使用 ESP-SR 的 AFE（Audio Front End）进行专业降噪
 * - 支持 AEC（回声消除）、NS（噪声抑制）、VAD（语音检测）
 * - 比 SpeexDSP 更强大，是 ESP32 官方推荐的音频处理方案
 * 
 * 使用方法：
 *   在 main.cc 中调用 i2s_mic_full_start();
 */

#include <esp_log.h>
#include <esp_afe_sr_models.h>
#include <esp_afe_sr_iface.h>
#include <model_path.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>

static const char* TAG = "I2SMicFull";

// ==================== 配置参数 ====================

// I2S 引脚配置
#define I2S_WS_PIN      42
#define I2S_SCK_PIN     41
#define I2S_DIN_PIN     2

// 音频参数
#define SAMPLE_RATE     16000
#define FRAME_SIZE      160    // 10ms @ 16kHz
#define CHANNEL_NUM     1      // 单声道

// ==================== 全局变量 ====================

static i2s_chan_handle_t g_i2s_rx_handle = nullptr;
static esp_afe_sr_iface_t* g_afe_handle = nullptr;
static esp_afe_sr_data_t* g_afe_data = nullptr;
static TaskHandle_t g_task_handle = nullptr;
static bool g_running = false;

// ==================== I2S 初始化 ====================

static bool init_i2s(void) {
    ESP_LOGI(TAG, "初始化 I2S: WS=%d, SCK=%d, DIN=%d", I2S_WS_PIN, I2S_SCK_PIN, I2S_DIN_PIN);
    
    // 创建 I2S 通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 1024;
    chan_cfg.auto_clear = true;
    
    if (i2s_new_channel(&chan_cfg, nullptr, &g_i2s_rx_handle) != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S 通道失败");
        return false;
    }
    
    // 配置 I2S 标准模式
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(I2S_SCK_PIN),
            .ws = static_cast<gpio_num_t>(I2S_WS_PIN),
            .dout = I2S_GPIO_UNUSED,
            .din = static_cast<gpio_num_t>(I2S_DIN_PIN),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    if (i2s_channel_init_std_mode(g_i2s_rx_handle, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "初始化 I2S 失败");
        i2s_del_channel(g_i2s_rx_handle);
        g_i2s_rx_handle = nullptr;
        return false;
    }
    
    if (i2s_channel_enable(g_i2s_rx_handle) != ESP_OK) {
        ESP_LOGE(TAG, "启用 I2S 失败");
        i2s_del_channel(g_i2s_rx_handle);
        g_i2s_rx_handle = nullptr;
        return false;
    }
    
    ESP_LOGI(TAG, "✓ I2S 初始化成功");
    return true;
}

// ==================== ESP-SR AFE 初始化 ====================

static bool init_afe(void) {
    ESP_LOGI(TAG, "初始化 ESP-SR AFE...");
    
    // 配置 AFE（单麦克风）
    afe_config_t afe_config = {};
    
    // AEC
    afe_config.aec_init = false;
    
    // SE
    afe_config.se_init = true;
    
    // NS
    afe_config.ns_init = true;
    afe_config.ns_model_name = nullptr;
    afe_config.afe_ns_mode = AFE_NS_MODE_WEBRTC;
    
    // VAD
    afe_config.vad_init = true;
    afe_config.vad_mode = VAD_MODE_3;
    afe_config.vad_model_name = nullptr;
    
    // WakeNet
    afe_config.wakenet_init = false;
    afe_config.wakenet_model_name = nullptr;
    afe_config.wakenet_mode = DET_MODE_90;
    
    // AGC
    afe_config.agc_init = false;
    
    // PCM Config
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.mic_ids = nullptr;
    afe_config.pcm_config.ref_num = 0;
    afe_config.pcm_config.ref_ids = nullptr;
    afe_config.pcm_config.sample_rate = SAMPLE_RATE;
    
    // General
    afe_config.afe_mode = AFE_MODE_LOW_COST;
    afe_config.afe_type = AFE_TYPE_VC;
    afe_config.afe_perferred_core = 1;
    afe_config.afe_perferred_priority = 5;
    afe_config.afe_ringbuf_size = 50;
    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.afe_linear_gain = 1.0f;
    afe_config.debug_init = false;
    afe_config.fixed_first_channel = false;
    
    #ifdef CONFIG_USE_AUDIO_PROCESSOR
    // 如果启用了音频处理器，尝试加载降噪模型
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models) {
        char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, nullptr);
        if (ns_model_name) {
            afe_config.afe_ns_mode = AFE_NS_MODE_NET;
            afe_config.ns_model_name = ns_model_name;
            ESP_LOGI(TAG, "使用神经网络降噪模型: %s", ns_model_name);
        }
    }
    #endif
    
    // 创建 AFE 实例
    g_afe_handle = esp_afe_handle_from_config(&afe_config);
    if (!g_afe_handle) {
        ESP_LOGE(TAG, "获取 AFE handle 失败");
        return false;
    }
    
    g_afe_data = g_afe_handle->create_from_config(&afe_config);
    
    if (!g_afe_data) {
        ESP_LOGE(TAG, "创建 AFE 失败");
        return false;
    }
    
    ESP_LOGI(TAG, "✓ ESP-SR AFE 初始化成功");
    ESP_LOGI(TAG, "  Feed 块大小: %d", g_afe_handle->get_feed_chunksize(g_afe_data));
    ESP_LOGI(TAG, "  Fetch 块大小: %d", g_afe_handle->get_fetch_chunksize(g_afe_data));
    
    return true;
}

// ==================== 音频处理任务 ====================

static void audio_process_task(void* arg) {
    ESP_LOGI(TAG, "音频处理任务开始");
    
    int feed_size = g_afe_handle->get_feed_chunksize(g_afe_data);
    
    int32_t* i2s_buffer = new int32_t[feed_size];
    int16_t* audio_buffer = new int16_t[feed_size];
    
    int frame_counter = 0;
    int frames_per_second = SAMPLE_RATE / feed_size;
    bool last_vad_state = false;
    
    while (g_running) {
        // 1. 读取 I2S 数据
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(
            g_i2s_rx_handle,
            i2s_buffer,
            feed_size * sizeof(int32_t),
            &bytes_read,
            portMAX_DELAY
        );
        
        if (ret != ESP_OK || bytes_read != feed_size * sizeof(int32_t)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // 2. 转换为 16-bit PCM
        for (int i = 0; i < feed_size; i++) {
            audio_buffer[i] = static_cast<int16_t>(i2s_buffer[i] >> 16);
        }
        
        // 3. 送入 AFE 处理
        g_afe_handle->feed(g_afe_data, audio_buffer);
        
        // 4. 获取处理结果
        afe_fetch_result_t* res = g_afe_handle->fetch(g_afe_data);
        if (res) {
            // VAD 状态检测
            bool is_voice = (res->vad_state == VAD_SPEECH);
            
            // VAD 状态变化
            if (is_voice != last_vad_state) {
                last_vad_state = is_voice;
                if (is_voice) {
                    ESP_LOGI(TAG, ">>> 检测到语音 <<<");
                } else {
                    ESP_LOGI(TAG, "<<< 语音结束 >>>");
                }
            }
            
            // 每秒输出一次状态
            if (++frame_counter >= frames_per_second) {
                frame_counter = 0;
                if (is_voice) {
                    ESP_LOGI(TAG, "🎤 Voice detected");
                } else {
                    ESP_LOGI(TAG, "… Silence");
                }
            }
            
            // 这里可以获取降噪后的音频数据
            // int16_t* clean_audio = res->data;
            // int clean_size = fetch_size;
            // 可以将 clean_audio 发送到语音识别引擎
        }
    }
    
    delete[] i2s_buffer;
    delete[] audio_buffer;
    
    ESP_LOGI(TAG, "音频处理任务结束");
    vTaskDelete(nullptr);
}

// ==================== 启动/停止函数 ====================

extern "C" void i2s_mic_full_start(void) {
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  I2S 麦克风 + ESP-SR AFE 专业降噪");
    ESP_LOGI(TAG, "  采样率: %d Hz", SAMPLE_RATE);
    ESP_LOGI(TAG, "  引脚: WS=%d, SCK=%d, DIN=%d", I2S_WS_PIN, I2S_SCK_PIN, I2S_DIN_PIN);
    ESP_LOGI(TAG, "==========================================");
    
    // 1. 初始化 I2S
    if (!init_i2s()) {
        ESP_LOGE(TAG, "❌ I2S 初始化失败");
        return;
    }
    
    // 2. 初始化 ESP-SR AFE
    if (!init_afe()) {
        ESP_LOGE(TAG, "❌ AFE 初始化失败");
        return;
    }
    
    // 3. 启动音频处理任务
    g_running = true;
    xTaskCreatePinnedToCore(
        audio_process_task,
        "audio_afe",
        8192,    // 更大的栈（AFE 需要）
        nullptr,
        5,
        &g_task_handle,
        1
    );
    
    ESP_LOGI(TAG, "✓ ESP-SR AFE 音频处理已启动");
    ESP_LOGI(TAG, "正在监听音频... 每 1 秒输出一次 VAD 状态");
}

extern "C" void i2s_mic_full_stop(void) {
    if (!g_running) {
        return;
    }
    
    g_running = false;
    
    if (g_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        g_task_handle = nullptr;
    }
    
    if (g_afe_data && g_afe_handle) {
        g_afe_handle->destroy(g_afe_data);
        g_afe_data = nullptr;
    }
    
    if (g_i2s_rx_handle) {
        i2s_channel_disable(g_i2s_rx_handle);
        i2s_del_channel(g_i2s_rx_handle);
        g_i2s_rx_handle = nullptr;
    }
    
    ESP_LOGI(TAG, "✓ ESP-SR AFE 已停止");
}

