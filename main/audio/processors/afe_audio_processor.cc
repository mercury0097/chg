#include "afe_audio_processor.h"
#include <esp_log.h>
#include <string.h>
#include <esp_partition.h>

#include "config.h"

#define PROCESSOR_RUNNING 0x01

#define TAG "AfeAudioProcessor"

AfeAudioProcessor::AfeAudioProcessor()
    : afe_data_(nullptr) {
    event_group_ = xEventGroupCreate();
}

void AfeAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;
    has_reference_ = codec_->input_reference();
    use_sr_frontend_ = codec_->microphone_channels() > 1;
    aec_available_ = false;
    device_aec_enabled_ = false;

    // Pre-allocate output buffer capacity
    output_buffer_.reserve(frame_samples_);

    int ref_num = has_reference_ ? 1 : 0;

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    // 🎯 直接读取 Flash 分区验证数据
    ESP_LOGI(TAG, "🔍 直接读取 model 分区验证烧录是否成功...");
    const esp_partition_t* model_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");
    if (model_part != nullptr) {
        uint8_t header[256];
        if (esp_partition_read(model_part, 0, header, sizeof(header)) == ESP_OK) {
            int model_count = (int)(header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24));
            ESP_LOGI(TAG, "📦 Flash 中的模型数量: %d", model_count);
            
            // 读取模型名称（从偏移 4 开始，每个模型名称占 32 字节）
            for (int i = 0; i < model_count && i < 10; i++) {
                char model_name[33] = {0};
                size_t offset = 4 + i * 256;  // 粗略估计
                if (offset + 32 < sizeof(header)) {
                    memcpy(model_name, &header[offset], 32);
                    model_name[32] = '\0';
                    if (strlen(model_name) > 0) {
                        ESP_LOGI(TAG, "   Flash 模型 %d: %s", i, model_name);
                    }
                }
            }
        }
    }
    
    // 双麦对话态优先复用已加载的模型列表，避免额外复制一份模型元数据。
    if (use_sr_frontend_ && models_list != nullptr) {
        ESP_LOGI(TAG, "🔍 复用已加载的 model 列表（dual-mic 路径）...");
        models_ = models_list;
        owns_models_ = false;
    } else {
        ESP_LOGI(TAG, "🔍 强制从 model 分区重新加载模型（忽略可能的缓存）...");
        models_ = esp_srmodel_init("model");
        owns_models_ = true;
    }

    srmodel_list_t *models = models_;
    if (models == nullptr) {
        ESP_LOGE(TAG, "❌ 从 model 分区加载模型失败！");
        return;
    }
    
    ESP_LOGI(TAG, "✅ ESP-SR 加载的模型数量: %d", models->num);
    // 打印所有模型名称
    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "   ESP-SR 模型 %d: %s", i, models->model_name[i]);
    }

    // 🎯 显式指定使用神经网络模型（避免 filter 返回旧版本）
    #ifdef CONFIG_SR_NSN_NSNET2
        char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, "nsnet2");
        ESP_LOGI(TAG, "🔍 指定降噪模型: nsnet2 (神经网络)");
    #else
        char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    #endif
    
    #ifdef CONFIG_SR_VADN_VADNET1_MEDIUM
        // 🎯 尝试多个可能的 VADNet1 模型名称（ESP-IDF 5.5 可能使用不同的名称）
        char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, "vadnet1_medium");
        if (vad_model_name == nullptr) {
            ESP_LOGW(TAG, "⚠️  未找到 vadnet1_medium，尝试 vadnet1...");
            vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, "vadnet1");
        }
        if (vad_model_name == nullptr) {
            ESP_LOGW(TAG, "⚠️  未找到 vadnet1，尝试 vadn1_medium...");
            vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, "vadn1_medium");
        }
        if (vad_model_name == nullptr) {
            ESP_LOGW(TAG, "⚠️  未找到 vadn1_medium，尝试 vadn1...");
            vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, "vadn1");
        }
        if (vad_model_name == nullptr) {
            ESP_LOGW(TAG, "⚠️  未找到任何 VADNet 模型，尝试不指定名称（使用第一个匹配的）...");
            vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
        }

        // 🎯 打印所有可用的 VAD 模型，帮助调试
        ESP_LOGI(TAG, "🔍 可用 VAD 模型列表:");
        for (int i = 0; i < models->num; i++) {
            if (strstr(models->model_name[i], "vad") != NULL) {
                ESP_LOGI(TAG, "   [%d] %s", i, models->model_name[i]);
            }
        }

        ESP_LOGI(TAG, "🔍 指定 VAD 模型: vadnet1_medium (神经网络), 找到: %s",
                 vad_model_name ? vad_model_name : "NULL");
    #else
        char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
        ESP_LOGI(TAG, "🔍 VAD 模型: 使用默认 (WebRTC)");
    #endif
    
    // 双麦板在对话阶段不能继续走 AFE_TYPE_VC，否则 ESP-SR 会退化成单麦模式。
    auto afe_type = use_sr_frontend_ ? AFE_TYPE_SR : AFE_TYPE_VC;
    ESP_LOGI(TAG,
             "🎛️  AFE communication frontend: %s (input_format=%s, mic=%d, ref=%d)",
             use_sr_frontend_ ? "AFE_TYPE_SR" : "AFE_TYPE_VC",
             input_format.c_str(), codec_->microphone_channels(), ref_num);

    // 对话态不需要 WakeNet，传空模型表避免把唤醒词链也挂进 VC/SR AFE。
    afe_config_t* afe_config =
        afe_config_init(input_format.c_str(), nullptr, afe_type,
                        AFE_MODE_LOW_COST);
    if (afe_config == nullptr) {
        ESP_LOGE(TAG, "Failed to create AFE config");
        return;
    }
    // 🛡️ 使用 SR_LOW_COST 模式的 AEC，VOIP 模式太耗 CPU 会触发看门狗
    afe_config->aec_mode = AEC_MODE_SR_LOW_COST;
    
    // 🎯 优化 VAD 参数以更好地检测人声
    afe_config->vad_mode = VAD_MODE_3;  // 最灵敏模式（0=不灵敏, 3=灵敏）
    afe_config->vad_min_noise_ms = 110;  // 增加噪声判定防抖，减少短暂停顿/串音导致的误切换
    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
        ESP_LOGI(TAG, "✅ VAD 人声检测: 神经网络模式 (%s, Level 3 高灵敏)", vad_model_name);
    } else {
        ESP_LOGI(TAG, "✅ VAD 人声检测: WebRTC 模式 (Level 3 高灵敏)");
    }

    if (ns_model_name != nullptr) {
        // 使用神经网络降噪模型（现在会被打包）
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
        ESP_LOGI(TAG, "✅ 使用 ESP-SR 神经网络降噪: %s", ns_model_name);
        ESP_LOGI(TAG, "   降噪模式: AFE_NS_MODE_NET (%d)", (int)afe_config->afe_ns_mode);
    } else {
        // 没有 NS 模型，禁用降噪
        afe_config->ns_init = false;
        ESP_LOGW(TAG, "⚠️  未找到 NS 降噪模型，降噪已禁用");
        ESP_LOGW(TAG, "   请运行: idf.py build (会自动打包 NS 模型)");
    }

    // 🎯 启用 AGC（自动增益控制）增强人声
    afe_config->agc_init = true;
    afe_config->agc_mode = AFE_AGC_MODE_WEBRTC;  // 使用 WEBRTC AGC
#if CONFIG_BOARD_TYPE_DOG1
    afe_config->agc_compression_gain_db = 11;     // Dog1: 适度增强，减少底噪被一并拉高
#else
    afe_config->agc_compression_gain_db = 15;     // 其他板型保留原配置
#endif
    afe_config->agc_target_level_dbfs = 3;        // 目标电平 -3dBFS（默认3）
    ESP_LOGI(TAG, "✅ AGC 自动增益控制: WEBRTC 模式 (增益=%ddB)",
             afe_config->agc_compression_gain_db);
    
    // 🎯 SE（语音增强）在硬件边缘状态下可能把“像人声的串音”也一并强化。
#if CONFIG_BOARD_TYPE_DOG1
    afe_config->se_init = DOG1_ENABLE_SE;
    ESP_LOGI(TAG, "✅ SE 语音增强: %s", DOG1_ENABLE_SE ? "已启用" : "已禁用");
#else
    afe_config->se_init = true;
    ESP_LOGI(TAG, "✅ SE 语音增强: 已启用（突出人声频段，抑制音乐）");
#endif
    
    // 双麦路径在唤醒词切 Listening 时最容易顶爆内存，改成内存均衡分配。
    afe_config->memory_alloc_mode =
        use_sr_frontend_ ? AFE_MEMORY_ALLOC_INTERNAL_PSRAM_BALANCE
                         : AFE_MEMORY_ALLOC_MORE_PSRAM;
    
    // 🎯 大幅增加 AFE Ringbuffer 大小，避免 Speaking 状态下缓冲区溢出
    // VADNet1 神经网络处理更耗时，需要更大的缓冲区防止数据丢失
    afe_config->afe_ringbuf_size = use_sr_frontend_ ? 1200 : 2000;
    
    // 🎯 AFE 任务固定到 CPU1（负载较轻的核心）
    // CPU0: audio_input(8) + 图形渲染 → 负载重
    // CPU1: audio_communication + audio_output + LVGL → 相对均衡
    afe_config->afe_perferred_core = 1;  // 固定到 CPU1
    
    // 🎯 提高 AFE 任务优先级，确保及时处理音频数据
    // 优先级 4（中等偏高）：高于播放任务(3)，但不会阻塞系统
    afe_config->afe_perferred_priority = 4;  // 从 2 提升到 4

    // Initialize the AEC engine only when device-side AEC is compiled in.
    // We still start in VAD mode and switch to AEC at runtime when requested.
#if CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = has_reference_;
#else
    afe_config->aec_init = false;
#endif
    aec_available_ = afe_config->aec_init;
    afe_config->vad_init = true;  // 始终启用 VAD 用于人声检测
    
    if (aec_available_) {
        ESP_LOGI(TAG, "ℹ️  AEC 回声消除: 已初始化，Listening 模式默认关闭");
    } else if (has_reference_) {
        ESP_LOGI(TAG, "ℹ️  AEC 回声消除: 未初始化，Listening 模式仅使用 VAD");
    } else {
        ESP_LOGI(TAG, "ℹ️  AEC 回声消除: 未启用（需要参考音频通道）");
    }

    // 🎯 显示优化后的配置
    ESP_LOGI(TAG, "   Ringbuffer 大小: %d, AFE 优先级: %d, AFE 核心: %d",
             afe_config->afe_ringbuf_size, afe_config->afe_perferred_priority, afe_config->afe_perferred_core);

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    if (afe_iface_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get AFE interface from config");
        return;
    }
    afe_data_ = afe_iface_->create_from_config(afe_config);
    if (afe_data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create AFE processor instance");
        return;
    }
    
    // 🎯 audio_communication 任务固定到 CPU1（避免 CPU0 过载）
    // 增加栈大小到 8KB，支持 Speaking 状态下 AEC + VAD + 唤醒词同时运行
    // 优先级 4：与 audio_output 任务相同，确保音频处理链路顺畅
    xTaskCreatePinnedToCore([](void* arg) {
        auto this_ = (AfeAudioProcessor*)arg;
        this_->AudioProcessorTask();
        vTaskDelete(NULL);
    }, "audio_communication", 4096 * 2, this, 4, NULL, 1);
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }
    vEventGroupDelete(event_group_);
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (afe_data_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    if ((xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) == 0) {
        return;
    }

    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    const size_t chunk_size =
        afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
    while (input_buffer_.size() >= chunk_size) {
        afe_iface_->feed(afe_data_, input_buffer_.data());
        input_buffer_.erase(input_buffer_.begin(),
                            input_buffer_.begin() + chunk_size);
    }
}

void AfeAudioProcessor::Start() {
    device_aec_enabled_ = false;
    if (afe_data_ != nullptr) {
        ESP_LOGI(TAG, "🎤 Listening 模式：VAD enabled%s",
                 aec_available_ ? ", AEC disabled" : "");
        if (aec_available_) {
            afe_iface_->disable_aec(afe_data_);
        }
        afe_iface_->enable_vad(afe_data_);
    }
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    input_buffer_.clear();
}

bool AfeAudioProcessor::IsRunning() {
    return xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING;
}

void AfeAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

void AfeAudioProcessor::AudioProcessorTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio communication task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, PROCESSOR_RUNNING, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if ((xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) == 0) {
            continue;
        }
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            if (res != nullptr) {
                ESP_LOGI(TAG, "Error code: %d", res->ret_value);
            }
            continue;
        }

        // VAD state change
        if (vad_state_change_callback_) {
            if (res->vad_state == VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                ESP_LOGI(TAG, "VAD: speech detected");
                vad_state_change_callback_(true);
            } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                ESP_LOGI(TAG, "VAD: silence detected");
                vad_state_change_callback_(false);
            }
        }

        if (output_callback_) {
            size_t samples = res->data_size / sizeof(int16_t);
            
            // Add data to buffer
            output_buffer_.insert(output_buffer_.end(), res->data, res->data + samples);
            
            // Output complete frames when buffer has enough data
            while (output_buffer_.size() >= frame_samples_) {
                if (output_buffer_.size() == frame_samples_) {
                    // If buffer size equals frame size, move the entire buffer
                    output_callback_(std::move(output_buffer_));
                    output_buffer_.clear();
                    output_buffer_.reserve(frame_samples_);
                } else {
                    // If buffer size exceeds frame size, copy one frame and remove it
                    output_callback_(std::vector<int16_t>(output_buffer_.begin(), output_buffer_.begin() + frame_samples_));
                    output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
                }
            }
        }
    }
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (!has_reference_) {
        ESP_LOGW(TAG, "Device AEC requested but no reference channel is available");
        return;
    }
    if (!aec_available_) {
        ESP_LOGW(TAG, "Device AEC requested but AEC was not initialized in the AFE");
        return;
    }
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        ESP_LOGI(TAG, "🔊 Device AEC enabled");
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
        device_aec_enabled_ = true;
#else
        ESP_LOGE(TAG, "Device AEC is not supported");
#endif
    } else {
        ESP_LOGI(TAG, "🎤 Device AEC disabled, VAD enabled");
        if (device_aec_enabled_) {
            afe_iface_->disable_aec(afe_data_);
            device_aec_enabled_ = false;
        }
        afe_iface_->enable_vad(afe_data_);
    }
}
