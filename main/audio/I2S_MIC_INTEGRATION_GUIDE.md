# I2S 麦克风降噪模块集成指南

## 🚀 快速集成（3 步完成）

### 步骤 1：更新 CMakeLists.txt

编辑 `/Users/machenyang/Desktop/xiaozhi-esp32-main/main/CMakeLists.txt`

在第 2 行的 `set(SOURCES ...)` 部分添加新文件：

```cmake
set(SOURCES "audio/audio_codec.cc"
            "audio/audio_service.cc"
            "audio/i2s_mic_processor.cc"      # ← 添加这一行
            "audio/i2s_mic_example.cc"         # ← 添加这一行（如果需要示例）
            "audio/codecs/no_audio_codec.cc"
            # ... 其他文件 ...
```

### 步骤 2：配置 SpeexDSP

在项目根目录运行：

```bash
cd /Users/machenyang/Desktop/xiaozhi-esp32-main
idf.py menuconfig
```

导航到并启用：
```
Component config → 
  ESP Speech Recognition → 
    ☑ Enable Speex DSP
    ☑ Enable Noise Suppression
    ☑ Enable VAD
```

保存并退出 (按 Q 然后 Y)。

### 步骤 3A：集成到现有代码（推荐）

在您的主应用中使用降噪模块：

```cpp
// 在您的 application.cc 或其他初始化代码中
#include "audio/i2s_mic_processor.h"

// 创建实例（建议作为类成员变量）
I2SMicProcessor* mic_processor = nullptr;

void InitializeAudio() {
    // 1. 创建处理器
    mic_processor = new I2SMicProcessor(I2S_NUM_0, 16000, 160);
    
    // 2. 初始化 I2S（根据您的硬件修改引脚）
    mic_processor->InitI2S(GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_2);
    
    // 3. 初始化 SpeexDSP
    mic_processor->InitSpeexDSP(-15, 90);
    
    // 4. 设置回调
    mic_processor->SetAudioCallback([](const int16_t* data, size_t size) {
        // 处理降噪后的音频数据
        // 例如：发送到语音识别引擎
    });
    
    mic_processor->SetVadCallback([](bool is_voice) {
        if (is_voice) {
            ESP_LOGI("APP", "检测到语音");
        }
    });
    
    // 5. 启动
    mic_processor->Start(5);
}
```

### 步骤 3B：运行独立测试（可选）

如果只想测试降噪功能，编辑 `i2s_mic_example.cc`：

```cpp
// 取消注释此行（在文件末尾附近）
#define I2S_MIC_STANDALONE_TEST

// 然后在您的 main.cc 中删除或注释掉现有的 app_main()
// 这样会使用 i2s_mic_example.cc 中的 app_main()
```

---

## 🔧 硬件引脚配置

### 默认引脚定义（ESP32-S3）

```cpp
#define I2S_WS_PIN      GPIO_NUM_42    // Word Select (LRCK)
#define I2S_SCK_PIN     GPIO_NUM_41    // Serial Clock (BCLK)
#define I2S_DIN_PIN     GPIO_NUM_2     // Serial Data In (DATA)
```

### 修改引脚

编辑 `i2s_mic_example.cc` 中的引脚定义，或在您的代码中传入不同的 GPIO 编号。

### 常见开发板引脚

| 开发板 | WS (LRCK) | SCK (BCLK) | DIN (DATA) |
|-------|----------|-----------|-----------|
| ESP32-S3-DevKit | GPIO_42 | GPIO_41 | GPIO_2 |
| ESP-BOX | GPIO_14 | GPIO_15 | GPIO_16 |
| 自定义板 | 根据原理图 | 根据原理图 | 根据原理图 |

---

## 📦 编译和烧录

```bash
cd /Users/machenyang/Desktop/xiaozhi-esp32-main

# 清理构建（首次编译推荐）
idf.py fullclean

# 编译
idf.py build

# 烧录到设备
idf.py -p /dev/ttyUSB0 flash

# 监视串口输出
idf.py -p /dev/ttyUSB0 monitor
```

---

## 📊 预期输出

串口监视器应该显示：

```
I (1234) I2SMicProcessor: ✓ I2S 初始化成功
I (1245) I2SMicProcessor: ✓ SpeexDSP 初始化成功
I (1256) I2SMicProcessor: ✓ 音频处理任务已启动
I (1267) I2SMicProcessor: 音频处理循环开始
I (2267) I2SMicProcessor: … Silence
I (3267) I2SMicProcessor: … Silence
I (4267) I2SMicProcessor: 🎤 Voice detected  ← 说话时
I (5267) I2SMicProcessor: 🎤 Voice detected
I (6267) I2SMicProcessor: … Silence  ← 停止说话
```

---

## ⚙️ 参数调优

### 降噪强度

```cpp
// 弱降噪（安静环境）
mic_processor->InitSpeexDSP(-5, 90);

// 标准降噪（正常环境）- 推荐
mic_processor->InitSpeexDSP(-15, 90);

// 强降噪（嘈杂环境）
mic_processor->InitSpeexDSP(-25, 90);
```

### VAD 灵敏度

```cpp
// 高灵敏度（容易触发）
mic_processor->InitSpeexDSP(-15, 95);

// 标准灵敏度 - 推荐
mic_processor->InitSpeexDSP(-15, 90);

// 低灵敏度（减少误触发）
mic_processor->InitSpeexDSP(-15, 75);
```

---

## 🔍 故障排查

### 问题 1：编译错误 "speex/speex_preprocess.h: No such file"

**解决方法：**
1. 确保运行了 `idf.py menuconfig` 并启用了 SpeexDSP
2. 检查 `sdkconfig` 文件中是否有 `CONFIG_USE_AUDIO_PROCESSOR=y`
3. 尝试 `idf.py fullclean` 后重新编译

### 问题 2：I2S 读取失败

**检查事项：**
- ✅ 引脚连接是否正确
- ✅ 麦克风是否正确供电（3.3V）
- ✅ GND 是否连接
- ✅ I2S_NUM_0 是否被其他模块占用

**调试命令：**
```cpp
// 在 i2s_mic_example.cc 中调用
TestRawI2SRead();  // 测试原始 I2S 读取
```

### 问题 3：VAD 总是检测到语音或总是静音

**调整方法：**
1. 检查麦克风增益是否合适
2. 调整 VAD 阈值（75-95 之间）
3. 修改降噪强度（-5 到 -25 dB）
4. 检查音频能量是否在合理范围（使用 `CalculateRMS()`）

### 问题 4：音频有杂音

**可能原因：**
- DMA 缓冲区太小
- CPU 负载过高
- I2S 时钟不稳定

**解决方法：**
```cpp
// 在 i2s_mic_processor.cc 的 InitI2S() 中修改
chan_cfg.dma_desc_num = 16;    // 增加到 16（默认 8）
chan_cfg.dma_frame_num = 2048; // 增加到 2048（默认 1024）
```

---

## 📈 性能监控

### 查看 CPU 使用率

```cpp
// 在 FreeRTOS 任务中添加
void MonitorCPU() {
    TaskStatus_t task_status;
    vTaskGetInfo(task_handle_, &task_status, pdTRUE, eRunning);
    ESP_LOGI("Monitor", "CPU 使用: %d%%", task_status.ulRunTimeCounter);
}
```

### 查看内存使用

```cpp
ESP_LOGI("Monitor", "堆内存: %d 字节", esp_get_free_heap_size());
ESP_LOGI("Monitor", "PSRAM: %d 字节", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

---

## 🎯 高级集成示例

### 与现有音频系统集成

如果项目已有 AudioProcessor 接口：

```cpp
class I2SMicAudioProcessor : public AudioProcessor {
private:
    I2SMicProcessor* mic_processor_;
    
public:
    void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) override {
        mic_processor_ = new I2SMicProcessor(I2S_NUM_0, 16000, frame_duration_ms * 16);
        mic_processor_->InitI2S(GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_2);
        mic_processor_->InitSpeexDSP(-15, 90);
        
        mic_processor_->SetAudioCallback([this](const int16_t* data, size_t size) {
            // 转发到现有音频处理流程
            std::vector<int16_t> vec(data, data + size);
            this->OnOutput(std::move(vec));
        });
    }
    
    // 实现其他接口方法...
};
```

### 录音功能

```cpp
std::vector<int16_t> recording_buffer;
bool is_recording = false;

void StartRecording() {
    is_recording = true;
    recording_buffer.clear();
    recording_buffer.reserve(16000 * 5); // 5 秒
}

void StopRecording() {
    is_recording = false;
    // 保存 recording_buffer 到文件或发送到服务器
}

// 在音频回调中
mic_processor->SetAudioCallback([](const int16_t* data, size_t size) {
    if (is_recording && recording_buffer.size() < recording_buffer.capacity()) {
        recording_buffer.insert(recording_buffer.end(), data, data + size);
    }
});
```

---

## 📚 相关文档

- [完整 API 文档](./I2S_MIC_NOISE_REDUCTION_README.md)
- [ICS-43434 数据手册](https://www.invensense.com/products/digital/ics-43434/)
- [ESP-IDF I2S 驱动](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html)
- [SpeexDSP 文档](https://www.speex.org/)

---

## ✅ 检查清单

在寻求帮助之前，请确认：

- [ ] 已在 menuconfig 中启用 SpeexDSP
- [ ] CMakeLists.txt 已添加新源文件
- [ ] 引脚配置与硬件匹配
- [ ] 麦克风正确连接并供电
- [ ] 已尝试 `idf.py fullclean && idf.py build`
- [ ] 串口输出显示初始化成功消息
- [ ] 已查看故障排查部分

---

**需要帮助？**
- 查看 [完整 README](./I2S_MIC_NOISE_REDUCTION_README.md)
- 检查代码中的详细注释
- 使用 `ESP_LOG_LEVEL_DEBUG` 查看详细日志

**祝您集成顺利！** 🎉

