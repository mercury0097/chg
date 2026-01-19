/**
 * @file eye_config.h
 * @brief 矢量眼睛参数配置结构
 * 
 * 移植自 esp32-eyes (Luis Llamas, Alastair Aitchison)
 * 适配 ESP-IDF + LVGL
 */

#pragma once

#include <cstdint>

namespace vector_eyes {

/**
 * @brief 眼睛形状配置参数
 * 
 * 通过调整这些参数可以创建不同的表情效果：
 * - offset: 眼睛位置偏移
 * - height/width: 眼睛大小
 * - slope: 眼睑倾斜度（负值=悲伤下垂，正值=愤怒上扬）
 * - radius: 眼角圆角大小
 * - inverse_radius: 内凹效果（用于笑眯眯的眼睛）
 */
struct EyeConfig {
    int16_t offset_x = 0;           ///< X轴偏移
    int16_t offset_y = 0;           ///< Y轴偏移
    int16_t height = 40;            ///< 眼睛高度
    int16_t width = 40;             ///< 眼睛宽度
    float slope_top = 0.0f;         ///< 上眼睑斜度 (-1.0 ~ 1.0)
    float slope_bottom = 0.0f;      ///< 下眼睑斜度
    int16_t radius_top = 8;         ///< 上眼角圆角
    int16_t radius_bottom = 8;      ///< 下眼角圆角
    int16_t inverse_radius_top = 0;     ///< 上眼睑内凹圆角
    int16_t inverse_radius_bottom = 0;  ///< 下眼睑内凹圆角
    int16_t inverse_offset_top = 0;     ///< 上眼睑内凹偏移
    int16_t inverse_offset_bottom = 0;  ///< 下眼睑内凹偏移

    /**
     * @brief 线性插值两个配置
     */
    static EyeConfig Lerp(const EyeConfig& a, const EyeConfig& b, float t) {
        EyeConfig result;
        result.offset_x = a.offset_x + (b.offset_x - a.offset_x) * t;
        result.offset_y = a.offset_y + (b.offset_y - a.offset_y) * t;
        result.height = a.height + (b.height - a.height) * t;
        result.width = a.width + (b.width - a.width) * t;
        result.slope_top = a.slope_top + (b.slope_top - a.slope_top) * t;
        result.slope_bottom = a.slope_bottom + (b.slope_bottom - a.slope_bottom) * t;
        result.radius_top = a.radius_top + (b.radius_top - a.radius_top) * t;
        result.radius_bottom = a.radius_bottom + (b.radius_bottom - a.radius_bottom) * t;
        result.inverse_radius_top = a.inverse_radius_top + (b.inverse_radius_top - a.inverse_radius_top) * t;
        result.inverse_radius_bottom = a.inverse_radius_bottom + (b.inverse_radius_bottom - a.inverse_radius_bottom) * t;
        result.inverse_offset_top = a.inverse_offset_top + (b.inverse_offset_top - a.inverse_offset_top) * t;
        result.inverse_offset_bottom = a.inverse_offset_bottom + (b.inverse_offset_bottom - a.inverse_offset_bottom) * t;
        return result;
    }
};

} // namespace vector_eyes
