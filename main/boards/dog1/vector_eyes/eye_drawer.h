/**
 * @file eye_drawer.h
 * @brief LVGL 眼睛绘制器
 *
 * 移植自 esp32-eyes EyeDrawer，使用 LVGL canvas 绘制
 */

#pragma once

#include "eye_config.h"
#include <lvgl.h>

namespace vector_eyes {

/**
 * @brief 圆角类型枚举
 */
enum class CornerType { TopRight, TopLeft, BottomLeft, BottomRight };

/**
 * @brief 眼睛绘制器（静态工具类）
 */
class EyeDrawer {
public:
  /**
   * @brief 设置绘图目标 canvas
   */
  static void SetCanvas(lv_obj_t *canvas);

  /**
   * @brief 设置绘图颜色
   */
  static void SetColor(lv_color_t color);

  /**
   * @brief 清空画布
   */
  static void Clear(lv_color_t bg_color);

  /**
   * @brief 获取背景颜色
   */
  static lv_color_t GetBgColor();

  /**
   * @brief 绘制完整眼睛
   * @param center_x 眼睛中心X
   * @param center_y 眼睛中心Y
   * @param config 眼睛配置参数
   */
  static void Draw(int16_t center_x, int16_t center_y, EyeConfig *config);

private:
  /**
   * @brief 填充矩形
   */
  static void FillRectangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            bool fill);

  /**
   * @brief 填充直角三角形
   */
  static void FillRectangularTriangle(int32_t x0, int32_t y0, int32_t x1,
                                      int32_t y1, bool fill);

  /**
   * @brief 填充椭圆角
   */
  static void FillEllipseCorner(CornerType corner, int16_t x0, int16_t y0,
                                int32_t rx, int32_t ry, bool fill);

  static lv_obj_t *canvas_;
  static lv_color_t draw_color_;
  static lv_color_t bg_color_;
};

} // namespace vector_eyes
