#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define POWER_CHARGE_DETECT_PIN GPIO_NUM_21
#define POWER_ADC_UNIT ADC_UNIT_2
#define POWER_ADC_CHANNEL ADC_CHANNEL_3

// 桌面小狗舵机定义（逻辑腿位与真实接线一致）
// 注意：舵机转轴平行于地面，脚可以垂直地面前后运动
// 实际接线：右前=IO39，右后=IO38，左后=IO17，左前=IO18
#define RIGHT_FRONT_LEG_PIN GPIO_NUM_39 // 右前腿
#define RIGHT_REAR_LEG_PIN GPIO_NUM_38  // 右后腿
#define LEFT_REAR_LEG_PIN GPIO_NUM_17   // 左后腿
#define LEFT_FRONT_LEG_PIN GPIO_NUM_18  // 左前腿

#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000 // 16kHz避免重采样，音质更好
#define AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_3
#define DISPLAY_MOSI_PIN GPIO_NUM_10
#define DISPLAY_CLK_PIN GPIO_NUM_9
#define DISPLAY_DC_PIN GPIO_NUM_46
#define DISPLAY_RST_PIN GPIO_NUM_11
#define DISPLAY_CS_PIN GPIO_NUM_2

// ST7789 135x240 横屏配置
// 注意: 物理屏幕135x240，横屏后逻辑分辨率240x135
// 芯片显存240x320，偏移值根据实际屏幕可能需要微调
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH 240     // 横屏后宽度 (原135x240屏幕)
#define DISPLAY_HEIGHT 135    // 横屏后高度
#define DISPLAY_MIRROR_X false  // 横屏模式镜像设置
#define DISPLAY_MIRROR_Y true   // 横屏模式镜像设置 (修复上下颠倒)
#define DISPLAY_SWAP_XY true    // 启用坐标轴交换实现横屏
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB

// 180°旋转开关：
// true  = 在当前横屏基础上再旋转180°（适配屏幕倒装）
// false = 使用原始方向
#define DISPLAY_ROTATE_180 true

// 偏移值说明: 根据实际显示效果调整
// 135x240屏幕横屏，芯片显存240x320
#define DISPLAY_OFFSET_X 40     // (320-240)/2=40
#define DISPLAY_OFFSET_Y 52     // (240-135)/2≈52
// 180°旋转后，Y方向通常需要+1像素补偿，避免上下边缘裁切
#define DISPLAY_OFFSET_X_ROTATED 40
#define DISPLAY_OFFSET_Y_ROTATED 53

#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3

#define BOOT_BUTTON_GPIO GPIO_NUM_0

#define DOG_VERSION "1.0.0"

#endif // _BOARD_CONFIG_H_

