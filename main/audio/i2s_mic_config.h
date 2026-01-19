/**
 * @file i2s_mic_config.h
 * @brief I2S 麦克风配置文件
 * 
 * 在这里集中配置所有 I2S 麦克风相关参数，
 * 方便不同硬件平台的适配。
 */

#pragma once

#include <driver/gpio.h>
#include <driver/i2s_types.h>

// ==================== I2S 硬件配置 ====================

/**
 * I2S 端口选择
 * - I2S_NUM_0: I2S 端口 0
 * - I2S_NUM_1: I2S 端口 1（如果有第二个麦克风）
 */
#ifndef I2S_MIC_PORT
#define I2S_MIC_PORT            I2S_NUM_0
#endif

/**
 * I2S 引脚配置（根据您的硬件修改）
 * 
 * 常见开发板配置：
 * 
 * ESP32-S3-DevKitC-1:
 *   WS_PIN  = GPIO_42
 *   SCK_PIN = GPIO_41
 *   DIN_PIN = GPIO_2
 * 
 * ESP-BOX:
 *   WS_PIN  = GPIO_14
 *   SCK_PIN = GPIO_15
 *   DIN_PIN = GPIO_16
 * 
 * ESP32-Korvo-2:
 *   WS_PIN  = GPIO_32
 *   SCK_PIN = GPIO_30
 *   DIN_PIN = GPIO_31
 */
#ifndef I2S_MIC_WS_PIN
#define I2S_MIC_WS_PIN          GPIO_NUM_42
#endif

#ifndef I2S_MIC_SCK_PIN
#define I2S_MIC_SCK_PIN         GPIO_NUM_41
#endif

#ifndef I2S_MIC_DIN_PIN
#define I2S_MIC_DIN_PIN         GPIO_NUM_2
#endif

// ==================== I2S 采样配置 ====================

/**
 * 采样率（Hz）
 * 推荐值：8000, 16000, 44100, 48000
 * 
 * 语音识别推荐：16000 Hz
 * 音乐录制推荐：44100 Hz 或 48000 Hz
 */
#ifndef I2S_MIC_SAMPLE_RATE
#define I2S_MIC_SAMPLE_RATE     16000
#endif

/**
 * 通道模式
 * - I2S_SLOT_MODE_MONO: 单声道
 * - I2S_SLOT_MODE_STEREO: 立体声
 */
#ifndef I2S_MIC_CHANNEL_MODE
#define I2S_MIC_CHANNEL_MODE    I2S_SLOT_MODE_MONO
#endif

/**
 * 位宽配置
 * ICS-43434 使用 32-bit 位宽（有效 24-bit）
 */
#ifndef I2S_MIC_BITS_PER_SAMPLE
#define I2S_MIC_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT
#endif

// ==================== DMA 缓冲区配置 ====================

/**
 * DMA 描述符数量
 * 范围：2-128
 * 
 * 较大值：更稳定，但占用更多内存
 * 较小值：占用少，但可能丢帧
 */
#ifndef I2S_MIC_DMA_DESC_NUM
#define I2S_MIC_DMA_DESC_NUM    8
#endif

/**
 * 每个 DMA 缓冲区的帧数
 * 范围：64-4096
 * 
 * 推荐值：1024（平衡延迟和稳定性）
 */
#ifndef I2S_MIC_DMA_FRAME_NUM
#define I2S_MIC_DMA_FRAME_NUM   1024
#endif

// ==================== 音频处理配置 ====================

/**
 * 每帧采样数
 * 
 * 计算公式：frame_samples = sample_rate * frame_duration_ms / 1000
 * 
 * 例如：
 *   16000 Hz, 10ms -> 160 采样
 *   16000 Hz, 20ms -> 320 采样
 *   16000 Hz, 30ms -> 480 采样
 * 
 * 推荐：160（10ms，低延迟）
 */
#ifndef I2S_MIC_FRAME_SAMPLES
#define I2S_MIC_FRAME_SAMPLES   160
#endif

/**
 * 音频处理任务优先级
 * 范围：0-25（数值越大优先级越高）
 * 
 * 推荐：5-10
 */
#ifndef I2S_MIC_TASK_PRIORITY
#define I2S_MIC_TASK_PRIORITY   5
#endif

/**
 * 音频处理任务栈大小（字节）
 */
#ifndef I2S_MIC_TASK_STACK_SIZE
#define I2S_MIC_TASK_STACK_SIZE 4096
#endif

/**
 * 音频处理任务固定到的核心
 * - 0: Core 0（通常用于协议栈）
 * - 1: Core 1（通常用于应用任务）
 * - tskNO_AFFINITY: 不固定
 */
#ifndef I2S_MIC_TASK_CORE
#define I2S_MIC_TASK_CORE       1
#endif

// ==================== SpeexDSP 降噪配置 ====================

/**
 * 降噪级别（dB）
 * 范围：-30 到 0
 * 
 * -5:  弱降噪（保留更多细节，适合安静环境）
 * -15: 标准降噪（推荐）
 * -25: 强降噪（适合嘈杂环境，可能损失音质）
 */
#ifndef SPEEX_NOISE_SUPPRESS_DB
#define SPEEX_NOISE_SUPPRESS_DB -15
#endif

/**
 * VAD 启动概率阈值
 * 范围：0-100
 * 
 * 95: 高灵敏度（容易触发）
 * 90: 标准灵敏度（推荐）
 * 75: 低灵敏度（减少误触发）
 */
#ifndef SPEEX_VAD_PROB_START
#define SPEEX_VAD_PROB_START    90
#endif

/**
 * 是否启用 AGC（自动增益控制）
 * 0: 禁用（推荐，避免放大噪声）
 * 1: 启用
 */
#ifndef SPEEX_ENABLE_AGC
#define SPEEX_ENABLE_AGC        0
#endif

/**
 * 是否启用去混响
 * 0: 禁用（推荐）
 * 1: 启用
 */
#ifndef SPEEX_ENABLE_DEREVERB
#define SPEEX_ENABLE_DEREVERB   0
#endif

// ==================== 能量门限降噪配置 ====================

/**
 * 初始噪声基准（RMS 能量）
 * 
 * 该值会在运行时自动调整
 */
#ifndef ENERGY_GATE_NOISE_FLOOR
#define ENERGY_GATE_NOISE_FLOOR 100.0f
#endif

/**
 * 噪声门限倍数
 * 
 * 如果音频能量低于 noise_floor * threshold，则认为是噪声
 */
#ifndef ENERGY_GATE_THRESHOLD
#define ENERGY_GATE_THRESHOLD   2.0f
#endif

/**
 * 噪声衰减系数
 * 范围：0.0-1.0
 * 
 * 0.0: 完全静音
 * 0.1: 衰减到 10%（推荐）
 * 1.0: 不衰减
 */
#ifndef ENERGY_GATE_ATTENUATION
#define ENERGY_GATE_ATTENUATION 0.1f
#endif

/**
 * 噪声基准更新间隔（帧数）
 * 
 * 每隔多少帧更新一次噪声估计
 */
#ifndef NOISE_UPDATE_INTERVAL
#define NOISE_UPDATE_INTERVAL   50
#endif

// ==================== VAD 输出配置 ====================

/**
 * VAD 状态输出间隔（秒）
 * 
 * 每隔多少秒输出一次"Voice detected"或"Silence"
 */
#ifndef VAD_OUTPUT_INTERVAL_SEC
#define VAD_OUTPUT_INTERVAL_SEC 1
#endif

// ==================== 调试配置 ====================

/**
 * 是否启用详细日志
 * 0: 仅输出关键信息
 * 1: 输出详细调试信息
 */
#ifndef I2S_MIC_DEBUG_VERBOSE
#define I2S_MIC_DEBUG_VERBOSE   0
#endif

/**
 * 是否启用性能监控
 * 0: 禁用
 * 1: 启用（输出 CPU 和内存使用情况）
 */
#ifndef I2S_MIC_ENABLE_PERF_MONITOR
#define I2S_MIC_ENABLE_PERF_MONITOR 0
#endif

// ==================== 麦克风特性配置 ====================

/**
 * ICS-43434 特定配置
 * 
 * ICS-43434 是 24-bit MEMS 麦克风，数据左对齐在 32-bit 中
 * 即：bit[31:8] 是有效数据，bit[7:0] 通常为 0
 */

/**
 * 麦克风类型
 * - 0: 通用 I2S 麦克风
 * - 1: ICS-43434（24-bit 左对齐）
 * - 2: INMP441（24-bit 左对齐）
 * - 3: SPH0645（18-bit）
 */
#ifndef I2S_MIC_TYPE
#define I2S_MIC_TYPE            1  // ICS-43434
#endif

/**
 * 数据转换方式
 * 
 * 对于 ICS-43434：
 *   右移 16 位：取高 16 位作为 16-bit PCM
 *   右移 8 位再除以 256：保留更多精度
 */
#ifndef I2S_MIC_BIT_SHIFT
#define I2S_MIC_BIT_SHIFT       16  // 右移位数
#endif

// ==================== 高级配置 ====================

/**
 * 是否使用能量门限预处理
 * 
 * 在 SpeexDSP 降噪之前先进行简单的能量门限滤波
 */
#ifndef ENABLE_ENERGY_GATING
#define ENABLE_ENERGY_GATING    1
#endif

/**
 * 音频缓冲区预分配大小
 * 
 * 在初始化时预分配缓冲区，减少运行时内存分配
 */
#ifndef AUDIO_BUFFER_RESERVE_SIZE
#define AUDIO_BUFFER_RESERVE_SIZE (I2S_MIC_FRAME_SAMPLES * 10)
#endif

// ==================== 平台特定配置 ====================

#if CONFIG_IDF_TARGET_ESP32
    // ESP32 特定配置
    #undef I2S_MIC_TASK_CORE
    #define I2S_MIC_TASK_CORE   0  // ESP32 使用 Core 0
#elif CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3 特定配置
    // 默认配置即可
#elif CONFIG_IDF_TARGET_ESP32C3
    // ESP32-C3 特定配置（单核）
    #undef I2S_MIC_TASK_CORE
    #define I2S_MIC_TASK_CORE   tskNO_AFFINITY
#endif

// ==================== 配置验证 ====================

#if I2S_MIC_FRAME_SAMPLES < 64 || I2S_MIC_FRAME_SAMPLES > 4096
    #error "I2S_MIC_FRAME_SAMPLES 必须在 64-4096 范围内"
#endif

#if I2S_MIC_SAMPLE_RATE < 8000 || I2S_MIC_SAMPLE_RATE > 48000
    #error "I2S_MIC_SAMPLE_RATE 必须在 8000-48000 范围内"
#endif

#if SPEEX_NOISE_SUPPRESS_DB > 0 || SPEEX_NOISE_SUPPRESS_DB < -30
    #error "SPEEX_NOISE_SUPPRESS_DB 必须在 -30 到 0 范围内"
#endif

#if SPEEX_VAD_PROB_START < 0 || SPEEX_VAD_PROB_START > 100
    #error "SPEEX_VAD_PROB_START 必须在 0-100 范围内"
#endif

// ==================== 使用示例 ====================

/*

在您的代码中使用这些配置：

```cpp
#include "i2s_mic_config.h"
#include "i2s_mic_processor.h"

void setup_microphone() {
    auto* mic = new I2SMicProcessor(
        I2S_MIC_PORT,
        I2S_MIC_SAMPLE_RATE,
        I2S_MIC_FRAME_SAMPLES
    );
    
    mic->InitI2S(
        I2S_MIC_WS_PIN,
        I2S_MIC_SCK_PIN,
        I2S_MIC_DIN_PIN
    );
    
    mic->InitSpeexDSP(
        SPEEX_NOISE_SUPPRESS_DB,
        SPEEX_VAD_PROB_START
    );
    
    mic->Start(I2S_MIC_TASK_PRIORITY);
}
```

要修改配置，可以：
1. 直接修改本文件中的 #define
2. 在编译时通过 -D 选项覆盖
3. 在 sdkconfig 中添加自定义配置

*/

