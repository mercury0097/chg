/**
 * @file verify_esp_sr.cc
 * @brief 验证 ESP-SR 是否正确启用和工作
 * 
 * 这个文件会进行全面的 ESP-SR 功能测试
 */

#include <esp_log.h>
#include <esp_afe_sr_models.h>
#include <esp_afe_sr_iface.h>
#include <model_path.h>
#include <esp_system.h>

static const char* TAG = "ESP-SR-Verify";

/**
 * @brief 验证 ESP-SR 组件是否可用
 */
extern "C" bool verify_esp_sr_available(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP-SR 组件验证");
    ESP_LOGI(TAG, "========================================");
    
    bool all_pass = true;
    
    // 1. 检查版本信息
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "1️⃣ 检查 ESP-SR 版本信息");
    ESP_LOGI(TAG, "   IDF 版本: %s", esp_get_idf_version());
    
    // 2. 检查 AFE 接口是否可用
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "2️⃣ 检查 AFE 接口");
    
    afe_config_t test_config = {};
    test_config.aec_init = false;
    test_config.se_init = false;
    test_config.ns_init = false;
    test_config.vad_init = true;
    test_config.wakenet_init = false;
    test_config.agc_init = false;
    test_config.pcm_config.total_ch_num = 1;
    test_config.pcm_config.mic_num = 1;
    test_config.pcm_config.ref_num = 0;
    test_config.pcm_config.sample_rate = 16000;
    test_config.afe_mode = AFE_MODE_LOW_COST;
    test_config.afe_type = AFE_TYPE_SR;
    test_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    
    esp_afe_sr_iface_t* afe_handle = esp_afe_handle_from_config(&test_config);
    if (afe_handle) {
        ESP_LOGI(TAG, "   ✅ AFE 接口创建成功");
        
        esp_afe_sr_data_t* afe_data = afe_handle->create_from_config(&test_config);
        if (afe_data) {
            ESP_LOGI(TAG, "   ✅ AFE 实例创建成功");
            ESP_LOGI(TAG, "   📊 Feed 块大小: %d", afe_handle->get_feed_chunksize(afe_data));
            ESP_LOGI(TAG, "   📊 Fetch 块大小: %d", afe_handle->get_fetch_chunksize(afe_data));
            
            // 清理测试实例
            afe_handle->destroy(afe_data);
        } else {
            ESP_LOGE(TAG, "   ❌ AFE 实例创建失败");
            all_pass = false;
        }
    } else {
        ESP_LOGE(TAG, "   ❌ AFE 接口创建失败");
        all_pass = false;
    }
    
    // 3. 检查可用的模型
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "3️⃣ 检查可用的模型");
    
    #ifdef CONFIG_USE_AUDIO_PROCESSOR
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models) {
        ESP_LOGI(TAG, "   ✅ 模型列表初始化成功");
        
        // 检查降噪模型
        char* ns_model = esp_srmodel_filter(models, ESP_NSNET_PREFIX, nullptr);
        if (ns_model) {
            ESP_LOGI(TAG, "   ✅ 神经网络降噪模型: %s", ns_model);
        } else {
            ESP_LOGI(TAG, "   ⚠️  未找到神经网络降噪模型（将使用 WEBRTC）");
        }
        
        // 检查 VAD 模型
        char* vad_model = esp_srmodel_filter(models, ESP_VADN_PREFIX, nullptr);
        if (vad_model) {
            ESP_LOGI(TAG, "   ✅ 神经网络 VAD 模型: %s", vad_model);
        } else {
            ESP_LOGI(TAG, "   ⚠️  未找到神经网络 VAD 模型（将使用 WEBRTC）");
        }
        
        // 检查唤醒词模型
        char* wn_model = esp_srmodel_filter(models, ESP_WN_PREFIX, nullptr);
        if (wn_model) {
            ESP_LOGI(TAG, "   ✅ 唤醒词模型: %s", wn_model);
        } else {
            ESP_LOGI(TAG, "   ⚠️  未找到唤醒词模型");
        }
    } else {
        ESP_LOGI(TAG, "   ⚠️  模型列表未初始化（可能模型目录不存在）");
    }
    #else
    ESP_LOGI(TAG, "   ⚠️  CONFIG_USE_AUDIO_PROCESSOR 未启用");
    #endif
    
    // 4. 内存检查
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "4️⃣ 内存状态检查");
    ESP_LOGI(TAG, "   可用堆内存: %lu 字节", esp_get_free_heap_size());
    ESP_LOGI(TAG, "   可用 PSRAM: %lu 字节", 
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // 推荐检查
    if (esp_get_free_heap_size() < 50000) {
        ESP_LOGW(TAG, "   ⚠️  堆内存不足，可能影响 AFE 性能");
    }
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 100000) {
        ESP_LOGW(TAG, "   ⚠️  PSRAM 不足，可能影响 AFE 性能");
    }
    
    // 5. 总结
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    if (all_pass) {
        ESP_LOGI(TAG, "  ✅ ESP-SR 验证通过！");
        ESP_LOGI(TAG, "  可以正常使用音频处理功能");
    } else {
        ESP_LOGE(TAG, "  ❌ ESP-SR 验证失败！");
        ESP_LOGE(TAG, "  请检查配置和依赖");
    }
    ESP_LOGI(TAG, "========================================");
    
    return all_pass;
}

/**
 * @brief 检查编译时链接的库
 */
extern "C" void verify_esp_sr_libraries(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📚 ESP-SR 编译信息");
    ESP_LOGI(TAG, "----------------------------------------");
    
    #ifdef CONFIG_USE_AUDIO_PROCESSOR
    ESP_LOGI(TAG, "✅ CONFIG_USE_AUDIO_PROCESSOR: 已启用");
    #else
    ESP_LOGE(TAG, "❌ CONFIG_USE_AUDIO_PROCESSOR: 未启用");
    #endif
    
    #ifdef CONFIG_ESP32_SPIRAM_SUPPORT
    ESP_LOGI(TAG, "✅ PSRAM 支持: 已启用");
    #else
    ESP_LOGW(TAG, "⚠️  PSRAM 支持: 未启用");
    #endif
    
    ESP_LOGI(TAG, "📊 芯片: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "📊 CPU 频率: %d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    
    ESP_LOGI(TAG, "----------------------------------------");
}

/**
 * @brief 运行完整的 ESP-SR 验证测试
 */
extern "C" void run_esp_sr_verification(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 开始 ESP-SR 完整验证...");
    ESP_LOGI(TAG, "");
    
    // 检查编译配置
    verify_esp_sr_libraries();
    
    // 检查运行时功能
    bool result = verify_esp_sr_available();
    
    if (result) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🎉 恭喜！ESP-SR 已正确配置并可以使用！");
        ESP_LOGI(TAG, "");
    } else {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "⚠️  ESP-SR 配置有问题，请检查！");
        ESP_LOGE(TAG, "");
    }
}



