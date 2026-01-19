# Palqiqi 开发板

## 简介

Palqiqi 是一个基于 ESP32-S3 的智能机器人开发板，源自 Otto 机器人平台。它具有多种动作能力和互动功能，集成了小智 AI 语音助手。

## 硬件特性

### 主控芯片
- ESP32-S3 (支持 WiFi + BLE)

### 显示屏
- 1.3 寸 ST7789 TFT LCD
- 分辨率: 240x240
- SPI 接口

### 舵机控制
- 左腿舵机: GPIO39
- 左脚舵机: GPIO38
- 右腿舵机: GPIO17
- 右脚舵机: GPIO18
- 左手舵机: GPIO8 (可选)
- 右手舵机: GPIO12 (可选)

### 音频
- I2S 麦克风输入 (16kHz)
- I2S 扬声器输出 (16kHz)
- 麦克风引脚: WS=GPIO4, SCK=GPIO5, DIN=GPIO6
- 扬声器引脚: DOUT=GPIO7, BCLK=GPIO15, LRCK=GPIO16

### 电源管理
- 充电检测: GPIO21
- 电池电压 ADC: ADC2_CH3

### 其他
- Boot 按钮: GPIO0
- 显示背光: GPIO3

## 硬件要求

- ESP32-S3 开发板 (16MB Flash 推荐)
- ST7789 240x240 TFT 显示屏
- I2S 数字麦克风 (如 INMP441)
- I2S 功放模块 (如 MAX98357A)
- 4-6 个 SG90 舵机
- 3.7V 锂电池 (可选)

## 编译和烧录

### 方法一：使用 idf.py (手动配置)

```bash
# 1. 设置 ESP-IDF 环境
. $IDF_PATH/export.sh

# 2. 配置板子类型
idf.py menuconfig
# 导航到: Xiaozhi Assistant -> Board Type -> Palqiqi

# 3. 编译
idf.py build

# 4. 烧录
idf.py -p /dev/ttyUSB0 flash
```

### 方法二：使用 release.py 脚本 (推荐)

```bash
# 一键编译
python scripts/release.py palqiqi

# 编译产物位于 build 目录
```

### 烧录固件

```bash
# 完整烧录 (包含分区表和固件)
idf.py -p /dev/ttyUSB0 flash

# 仅烧录应用程序
idf.py -p /dev/ttyUSB0 app-flash

# 监控串口输出
idf.py -p /dev/ttyUSB0 monitor
```

## 功能概述

Palqiqi 机器人具有丰富的动作能力，包括行走、转向、跳跃、摇摆等多种舞蹈动作。

### 动作列表

| 动作 | 描述 | 主要参数 |
|------|------|----------|
| walk_forward | 行走 | steps, speed, direction |
| turn_left | 转身 | steps, speed, direction |
| jump | 跳跃 | steps, speed |
| swing | 左右摇摆 | steps, speed, amount |
| moonwalk | 太空步 | steps, speed, direction |
| bend | 弯曲身体 | steps, speed, direction |
| shake_leg | 摇腿 | steps, speed, direction |
| updown | 上下运动 | steps, speed, amount |
| hands_up | 举手 * | speed, direction |
| hands_down | 放手 * | speed, direction |
| hand_wave | 挥手 * | speed, direction |

**注**: 标记 * 的手部动作仅在配置了手部舵机时可用。

### 参数说明

- **steps**: 动作执行的步数/次数 (1-100)
- **speed**: 动作执行速度 (500-1500，数值越小越快)
- **direction**: 方向参数 (1=左/前进, -1=右/后退)
- **amount**: 动作幅度 (0-170度)

## 显示模式

Palqiqi 支持两种显示模式：

1. **矢量眼睛模式**: 动态矢量图形眼睛，支持眨眼和表情变化
2. **GIF 表情模式**: 播放预设的 GIF 动画表情

## 版本信息

当前版本: 1.4.4

## 相关链接

- [自定义开发板指南](../../../docs/custom-board.md)
- [Otto 机器人官网](https://www.ottodiy.tech)
