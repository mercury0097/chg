/**
 * @file eye_drawer.cc
 * @brief LVGL 眼睛绘制器实现 - Cozmo 风格优化版
 *
 * 特点：
 * 1. 使用 LVGL 的批量绘制 API，高性能
 * 2. 支持发光效果（可选）
 * 3. 更精确的斜边绘制
 */

#include "eye_drawer.h"
#include <algorithm>
#include <cmath>

namespace vector_eyes {

// 静态成员初始化
lv_obj_t *EyeDrawer::canvas_ = nullptr;
lv_color_t EyeDrawer::draw_color_ = lv_color_white();
lv_color_t EyeDrawer::bg_color_ = lv_color_black();

void EyeDrawer::SetCanvas(lv_obj_t *canvas) { canvas_ = canvas; }

void EyeDrawer::SetColor(lv_color_t color) { draw_color_ = color; }

void EyeDrawer::Clear(lv_color_t bg_color) {
  bg_color_ = bg_color;
  if (canvas_) {
    lv_canvas_fill_bg(canvas_, bg_color, LV_OPA_COVER);
  }
}

// 高性能：使用 LVGL layer 绘制填充矩形
static void DrawRect(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w,
                     int32_t h, lv_color_t color, int16_t radius = 0) {
  if (!canvas || w <= 0 || h <= 0)
    return;

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = radius;
  dsc.border_width = 0;

  lv_area_t area = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(&layer, &dsc, &area);

  lv_canvas_finish_layer(canvas, &layer);
}

// 绘制带发光效果的矩形（Cozmo风格）
static void DrawGlowRect(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w,
                         int32_t h, lv_color_t color, int16_t radius = 0) {
  if (!canvas || w <= 0 || h <= 0)
    return;

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  // 外发光层（半透明）
  lv_draw_rect_dsc_t glow_dsc;
  lv_draw_rect_dsc_init(&glow_dsc);
  glow_dsc.bg_color = color;
  glow_dsc.bg_opa = LV_OPA_30; // 30% 透明度
  glow_dsc.radius = radius + 3;
  glow_dsc.border_width = 0;

  // 稍大的发光区域
  lv_area_t glow_area = {x - 3, y - 3, x + w + 2, y + h + 2};
  lv_draw_rect(&layer, &glow_dsc, &glow_area);

  // 主眼睛形状
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = radius;
  dsc.border_width = 0;

  lv_area_t area = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(&layer, &dsc, &area);

  lv_canvas_finish_layer(canvas, &layer);
}

// 绘制三角形（用于斜边效果）
static void DrawTriangle(lv_obj_t *canvas, int32_t x1, int32_t y1, int32_t x2,
                         int32_t y2, int32_t x3, int32_t y3, lv_color_t color) {
  if (!canvas)
    return;

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  lv_draw_triangle_dsc_t dsc;
  lv_draw_triangle_dsc_init(&dsc);
  dsc.color = color;

  dsc.p[0].x = x1;
  dsc.p[0].y = y1;
  dsc.p[1].x = x2;
  dsc.p[1].y = y2;
  dsc.p[2].x = x3;
  dsc.p[2].y = y3;

  lv_draw_triangle(&layer, &dsc);

  lv_canvas_finish_layer(canvas, &layer);
}

// 绘制笑眼（上凸下平，填充面积大）
static void DrawHappyArc(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w,
                         int32_t h, int32_t arc_height, lv_color_t color) {
  if (!canvas || w <= 0 || h <= 0)
    return;

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);

  // 方案：画一个圆角矩形，然后上边用大弧度圆角
  // 下边用很小的圆角（几乎是平的）
  
  int32_t total_height = arc_height + h;  // 总高度
  int32_t top_radius = arc_height;        // 上边圆角 = 弧形高度
  int32_t bottom_radius = 8;              // 下边很小的圆角，几乎是平的

  // 发光效果
  lv_draw_rect_dsc_t glow_dsc;
  lv_draw_rect_dsc_init(&glow_dsc);
  glow_dsc.bg_color = color;
  glow_dsc.bg_opa = LV_OPA_30;
  glow_dsc.radius = top_radius;
  glow_dsc.border_width = 0;

  lv_area_t glow_area = {x - 3, y - 3, x + w + 2, y + total_height + 2};
  lv_draw_rect(&layer, &glow_dsc, &glow_area);

  // 主体：上边大圆角，下边小圆角
  // 先画一个上边大圆角的矩形
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = top_radius;
  dsc.border_width = 0;

  lv_area_t area = {x, y, x + w - 1, y + total_height - 1};
  lv_draw_rect(&layer, &dsc, &area);

  // 用背景色矩形把下半部分的圆角盖住，让下边变平
  lv_draw_rect_dsc_t cover_dsc;
  lv_draw_rect_dsc_init(&cover_dsc);
  cover_dsc.bg_color = color;
  cover_dsc.bg_opa = LV_OPA_COVER;
  cover_dsc.radius = bottom_radius;
  cover_dsc.border_width = 0;

  // 下半部分用小圆角矩形覆盖
  lv_area_t bottom_area = {x, y + total_height / 2, x + w - 1, y + total_height - 1};
  lv_draw_rect(&layer, &cover_dsc, &bottom_area);

  lv_canvas_finish_layer(canvas, &layer);
}

// 绘制梯形（更精确的斜边效果）
static void DrawTrapezoid(lv_obj_t *canvas, int32_t x, int32_t y, int32_t w,
                          int32_t h, float slope_top, float slope_bottom,
                          lv_color_t color, int16_t radius) {
  if (!canvas || w <= 0 || h <= 0)
    return;

  // 发光层扩展距离
  const int32_t GLOW_EXTEND = 4;

  // 绘制基础矩形（带发光效果）
  DrawGlowRect(canvas, x, y, w, h, color, radius);

  // 用背景色三角形削掉边角，形成斜边
  // 三角形区域需要扩大，覆盖发光层
  if (slope_top != 0) {
    // 简化计算以降低CPU负担
    int32_t cut_height = h / 2 + GLOW_EXTEND; // 固定切掉一半高度
    int32_t cut_width = (int32_t)(std::abs(slope_top) * w * 0.5f) + GLOW_EXTEND;

    if (slope_top > 0) {
      // 外高内低（愤怒）：削掉左上角
      DrawTriangle(canvas, x - GLOW_EXTEND - 1,
                   y - GLOW_EXTEND - 1,                 // 左上角扩展
                   x + cut_width, y - GLOW_EXTEND - 1,  // 右上角
                   x - GLOW_EXTEND - 1, y + cut_height, // 左下角
                   EyeDrawer::GetBgColor());
    } else {
      // 内高外低（悲伤）：削掉右上角
      DrawTriangle(canvas, x + w + GLOW_EXTEND,
                   y - GLOW_EXTEND - 1,                    // 右上角扩展
                   x + w - cut_width, y - GLOW_EXTEND - 1, // 左上角
                   x + w + GLOW_EXTEND, y + cut_height,    // 右下角
                   EyeDrawer::GetBgColor());
    }
  }

  if (slope_bottom != 0) {
    int32_t cut_height = h / 3 + GLOW_EXTEND;
    int32_t cut_width =
        (int32_t)(std::abs(slope_bottom) * w * 0.5f) + GLOW_EXTEND;

    if (slope_bottom > 0) {
      // 削掉左下角
      DrawTriangle(canvas, x - GLOW_EXTEND - 1,
                   y + h + GLOW_EXTEND,                     // 左下角扩展
                   x + cut_width, y + h + GLOW_EXTEND,      // 右下角
                   x - GLOW_EXTEND - 1, y + h - cut_height, // 左上角
                   EyeDrawer::GetBgColor());
    } else {
      // 削掉右下角
      DrawTriangle(canvas, x + w + GLOW_EXTEND,
                   y + h + GLOW_EXTEND,                     // 右下角扩展
                   x + w - cut_width, y + h + GLOW_EXTEND,  // 左下角
                   x + w + GLOW_EXTEND, y + h - cut_height, // 右上角
                   EyeDrawer::GetBgColor());
    }
  }
}

void EyeDrawer::FillRectangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                              bool fill) {
  if (!canvas_)
    return;

  int32_t l = std::min(x0, x1);
  int32_t r = std::max(x0, x1);
  int32_t t = std::min(y0, y1);
  int32_t b = std::max(y0, y1);

  lv_color_t color = fill ? draw_color_ : bg_color_;
  DrawRect(canvas_, l, t, r - l, b - t, color, 0);
}

void EyeDrawer::FillRectangularTriangle(int32_t x0, int32_t y0, int32_t x1,
                                        int32_t y1, bool fill) {
  // 暂不使用
}

void EyeDrawer::FillEllipseCorner(CornerType corner, int16_t x0, int16_t y0,
                                  int32_t rx, int32_t ry, bool fill) {
  // 暂不使用
}

lv_color_t EyeDrawer::GetBgColor() { return bg_color_; }

void EyeDrawer::Draw(int16_t center_x, int16_t center_y, EyeConfig *config) {
  if (!canvas_ || !config)
    return;

  int16_t w = config->width;
  int16_t h = config->height;

  if (w < 2 || h < 2)
    return;

  // 计算边界
  int32_t left = center_x + config->offset_x - w / 2;
  int32_t top = center_y + config->offset_y - h / 2;

  // 检查是否使用弧形绘制（inverse_offset_top > 0 表示笑眼弧形模式）
  if (config->inverse_offset_top > 0) {
    // 笑眼弧形模式：上凸下平的月牙形
    DrawHappyArc(canvas_, left, top, w, h, config->inverse_offset_top, draw_color_);
    return;
  }

  // 获取参数
  float slope_top = config->slope_top;
  float slope_bottom = config->slope_bottom;

  // 计算圆角（取平均值，确保不超过一半）
  int16_t r_top = std::min(config->radius_top,
                           static_cast<int16_t>(std::min(w / 2, (int)h / 2)));
  int16_t r_bottom = std::min(
      config->radius_bottom, static_cast<int16_t>(std::min(w / 2, (int)h / 2)));
  int16_t avg_radius = (r_top + r_bottom) / 2;

  // 绘制主眼睛形状（带发光效果）
  DrawTrapezoid(canvas_, left, top, w, h, slope_top, slope_bottom, draw_color_,
                avg_radius);

  // 处理反向圆角（内凹效果，用于 Glee 笑脸）
  if (config->inverse_radius_bottom > 0) {
    int16_t inv_r = config->inverse_radius_bottom;
    // 在底部中间画一个背景色的半圆
    int32_t cx = left + w / 2;
    int32_t cy = top + h - inv_r / 2;
    DrawRect(canvas_, cx - inv_r, cy, inv_r * 2, inv_r, bg_color_, inv_r);
  }

  // 处理上眼睑内凹（如果需要）
  if (config->inverse_radius_top > 0) {
    int16_t inv_r = config->inverse_radius_top;
    int32_t cx = left + w / 2;
    int32_t cy = top + inv_r / 2;
    DrawRect(canvas_, cx - inv_r, cy - inv_r, inv_r * 2, inv_r, bg_color_,
             inv_r);
  }
}

} // namespace vector_eyes
