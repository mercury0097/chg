/**
 * @file eye_presets.h
 * @brief 表情预设参数 - 精确复刻 Cozmo 风格
 *
 * 根据 Anki Cozmo 机器人表情图片精确调整
 * 支持左右眼不对称表情
 */

#pragma once

#include "emotions.h"
#include "eye_config.h"

namespace vector_eyes {

// ============ Cozmo 表情预设 - 精确复刻版 ============

/**
 * 根据 Cozmo 图片分析：
 * - Neutral: 两个方形大眼，圆角
 * - Happy: 弯月形，上弧下平
 * - Glee: 更扁的弯月眼
 * - Sad: 内高外低眉毛，眼睛位置下移
 * - Worried: 轻微内高外低
 * - Focused: 很窄的横条
 * - Annoyed: 半闭，上边微斜
 * - Surprised: 大圆眼
 * - Skeptic: 左右眼不对称！左眼抬眉右眼正常
 * - Frustrated: 半闭无精打采
 * - Unimpressed: 半闭无语
 * - Sleepy: 几乎闭合
 * - Suspicious: 眯眼带斜度
 * - Squint: 眼睛向内偏移
 * - Angry: 外高内低愤怒
 * - Furious: 更强愤怒
 * - Scared: 大眼+担忧眉
 * - Awe: 大圆眼
 */

// ============ 通用表情（左右眼对称）============

// Neutral: 标准方形大眼睛 (Cozmo 最常见状态)
constexpr EyeConfig Preset_Normal = {.offset_x = 0,
                                     .offset_y = 0,
                                     .height = 75, // 大眼睛
                                     .width = 60,  // 略宽于高
                                     .slope_top = 0,
                                     .slope_bottom = 0,
                                     .radius_top = 12, // 圆角
                                     .radius_bottom = 12,
                                     .inverse_radius_top = 0,
                                     .inverse_radius_bottom = 0,
                                     .inverse_offset_top = 0,
                                     .inverse_offset_bottom = 0};

// Happy: 笑眼 😊 - 上凸下平，填充面积大
constexpr EyeConfig Preset_Happy = {.offset_x = 0,
                                    .offset_y = -5,   // 位置上移
                                    .height = 25,     // 下半部分高度
                                    .width = 75,      // 宽度
                                    .slope_top = 0,
                                    .slope_bottom = 0,
                                    .radius_top = 0,
                                    .radius_bottom = 0,
                                    .inverse_radius_top = 0,
                                    .inverse_radius_bottom = 0,
                                    .inverse_offset_top = 20,  // 上边弧形高度
                                    .inverse_offset_bottom = 0};

// Glee: 极度开心，比Happy更扁 (极致开心)
constexpr EyeConfig Preset_Glee = {.offset_x = 0,
                                   .offset_y = 12,
                                   .height = 22, // 非常扁
                                   .width = 80,  // 非常宽
                                   .slope_top = 0,
                                   .slope_bottom = 0,
                                   .radius_top = 22, // 完全圆弧
                                   .radius_bottom = 3,
                                   .inverse_radius_top = 0,
                                   .inverse_radius_bottom = 0,
                                   .inverse_offset_top = 0,
                                   .inverse_offset_bottom = 0};

// Sad: 悲伤，内高外低眉毛 (眼睛下垂)
constexpr EyeConfig Preset_Sad = {.offset_x = 0,
                                  .offset_y = 18, // 眼睛位置下移
                                  .height = 55,
                                  .width = 55,
                                  .slope_top =
                                      -0.8f, // 强烈的内高外低（倒八字眉）
                                  .slope_bottom = 0,
                                  .radius_top = 6,
                                  .radius_bottom = 15,
                                  .inverse_radius_top = 0,
                                  .inverse_radius_bottom = 0,
                                  .inverse_offset_top = 0,
                                  .inverse_offset_bottom = 0};

// Worried: 担忧，轻微的内高外低
constexpr EyeConfig Preset_Worried = {.offset_x = 0,
                                      .offset_y = 10,
                                      .height = 65,
                                      .width = 55,
                                      .slope_top = -0.45f, // 轻微下垂眉
                                      .slope_bottom = 0,
                                      .radius_top = 10,
                                      .radius_bottom = 12,
                                      .inverse_radius_top = 0,
                                      .inverse_radius_bottom = 0,
                                      .inverse_offset_top = 0,
                                      .inverse_offset_bottom = 0};

// Focused/Determined: 窄长横条眼 (专注/决心)
constexpr EyeConfig Preset_Focused = {.offset_x = 0,
                                      .offset_y = 0,
                                      .height = 22,       // 非常窄
                                      .width = 80,        // 很宽
                                      .slope_top = 0.25f, // 轻微外高内低
                                      .slope_bottom = -0.25f,
                                      .radius_top = 4,
                                      .radius_bottom = 4,
                                      .inverse_radius_top = 0,
                                      .inverse_radius_bottom = 0,
                                      .inverse_offset_top = 0,
                                      .inverse_offset_bottom = 0};

// Annoyed: 烦躁，半闭眼 (不耐烦)
constexpr EyeConfig Preset_Annoyed = {.offset_x = 0,
                                      .offset_y = -5,
                                      .height = 22,
                                      .width = 75,
                                      .slope_top = 0.2f, // 轻微斜度
                                      .slope_bottom = 0,
                                      .radius_top = 3,
                                      .radius_bottom = 18,
                                      .inverse_radius_top = 0,
                                      .inverse_radius_bottom = 0,
                                      .inverse_offset_top = 0,
                                      .inverse_offset_bottom = 0};

// Surprised: 超大圆眼 (惊讶)
constexpr EyeConfig Preset_Surprised = {.offset_x = 0,
                                        .offset_y = -5,
                                        .height = 90, // 非常大
                                        .width = 85,
                                        .slope_top = 0,
                                        .slope_bottom = 0,
                                        .radius_top = 42, // 接近圆形
                                        .radius_bottom = 42,
                                        .inverse_radius_top = 0,
                                        .inverse_radius_bottom = 0,
                                        .inverse_offset_top = 0,
                                        .inverse_offset_bottom = 0};

// ============ Skeptic: 左右眼不对称！============
// 左眼 (抬眉，挑眉效果)
constexpr EyeConfig Preset_Skeptic_Left = {.offset_x = 0,
                                           .offset_y = -8, // 向上
                                           .height = 75,
                                           .width = 60,
                                           .slope_top =
                                               0.5f, // 外高内低（挑眉）
                                           .slope_bottom = 0,
                                           .radius_top = 10,
                                           .radius_bottom = 12,
                                           .inverse_radius_top = 0,
                                           .inverse_radius_bottom = 0,
                                           .inverse_offset_top = 0,
                                           .inverse_offset_bottom = 0};

// 右眼 (正常)
constexpr EyeConfig Preset_Skeptic_Right = {.offset_x = 0,
                                            .offset_y = 0,
                                            .height = 65,
                                            .width = 55,
                                            .slope_top = 0,
                                            .slope_bottom = 0,
                                            .radius_top = 10,
                                            .radius_bottom = 10,
                                            .inverse_radius_top = 0,
                                            .inverse_radius_bottom = 0,
                                            .inverse_offset_top = 0,
                                            .inverse_offset_bottom = 0};

// 保留兼容性的通用 Skeptic（右眼）
constexpr EyeConfig Preset_Skeptic = Preset_Skeptic_Right;

// Frustrated/Bored: 沮丧/无聊，半闭无精打采
constexpr EyeConfig Preset_Frustrated = {.offset_x = 0,
                                         .offset_y = 0,
                                         .height = 20,
                                         .width = 75,
                                         .slope_top = 0.35f,
                                         .slope_bottom = 0,
                                         .radius_top = 3,
                                         .radius_bottom = 18,
                                         .inverse_radius_top = 0,
                                         .inverse_radius_bottom = 0,
                                         .inverse_offset_top = 0,
                                         .inverse_offset_bottom = 0};

// Unimpressed: 无语，半闭平视
constexpr EyeConfig Preset_Unimpressed = {.offset_x = 0,
                                          .offset_y = 0,
                                          .height = 18,
                                          .width = 75,
                                          .slope_top = 0,
                                          .slope_bottom = 0,
                                          .radius_top = 3,
                                          .radius_bottom = 14,
                                          .inverse_radius_top = 0,
                                          .inverse_radius_bottom = 0,
                                          .inverse_offset_top = 0,
                                          .inverse_offset_bottom = 0};

// Sleepy Eyes: 困倦，几乎闭合
constexpr EyeConfig Preset_Sleepy = {.offset_x = 0,
                                     .offset_y = 8,
                                     .height = 12, // 非常窄，快闭眼
                                     .width = 65,
                                     .slope_top = -0.35f, // 下垂
                                     .slope_bottom = -0.35f,
                                     .radius_top = 4,
                                     .radius_bottom = 4,
                                     .inverse_radius_top = 0,
                                     .inverse_radius_bottom = 0,
                                     .inverse_offset_top = 0,
                                     .inverse_offset_bottom = 0};

// Suspicious: 怀疑，眯眼带斜度
constexpr EyeConfig Preset_Suspicious = {.offset_x = 0,
                                         .offset_y = 0,
                                         .height = 35,
                                         .width = 70,
                                         .slope_top = 0.25f,
                                         .slope_bottom = -0.25f,
                                         .radius_top = 6,
                                         .radius_bottom = 4,
                                         .inverse_radius_top = 0,
                                         .inverse_radius_bottom = 0,
                                         .inverse_offset_top = 0,
                                         .inverse_offset_bottom = 0};

// Squint: 斜视，眼睛向内偏移
constexpr EyeConfig Preset_Squint = {.offset_x = -18, // 向内偏移
                                     .offset_y = 0,
                                     .height = 55,
                                     .width = 50,
                                     .slope_top = 0,
                                     .slope_bottom = 0,
                                     .radius_top = 10,
                                     .radius_bottom = 10,
                                     .inverse_radius_top = 0,
                                     .inverse_radius_bottom = 0,
                                     .inverse_offset_top = 0,
                                     .inverse_offset_bottom = 0};

// Angry: 生气，强烈倒八字愤怒眉（从外侧顶角开始深度切割）
constexpr EyeConfig Preset_Angry = {.offset_x = -5,     // 向内偏移，增加压迫感
                                    .offset_y = 2,       // 略微下移
                                    .height = 40,        // 增加高度让切割更明显
                                    .width = 60,         // 略微收窄
                                    .slope_top = 1.0f,   // 最大倾斜度，从顶角深度切割
                                    .slope_bottom = 0,   // 下边缘保持水平
                                    .radius_top = 1,     // 极小圆角，尖锐效果
                                    .radius_bottom = 15,
                                    .inverse_radius_top = 0,
                                    .inverse_radius_bottom = 0,
                                    .inverse_offset_top = 0,
                                    .inverse_offset_bottom = 0};

// ============ Furious: 左右眼不对称！============
// 左眼 (位置偏下)
constexpr EyeConfig Preset_Furious_Left = {.offset_x = 0,
                                           .offset_y = 8, // 偏下
                                           .height = 45,
                                           .width = 65,
                                           .slope_top = 0.8f, // 强烈愤怒眉毛
                                           .slope_bottom = 0,
                                           .radius_top = 3,
                                           .radius_bottom = 15,
                                           .inverse_radius_top = 0,
                                           .inverse_radius_bottom = 0,
                                           .inverse_offset_top = 0,
                                           .inverse_offset_bottom = 0};

// 右眼 (位置偏上，更大)
constexpr EyeConfig Preset_Furious_Right = {.offset_x = 0,
                                            .offset_y = -8, // 偏上
                                            .height = 55,
                                            .width = 70,
                                            .slope_top = 0.9f, // 更强愤怒眉毛
                                            .slope_bottom = 0,
                                            .radius_top = 3,
                                            .radius_bottom = 18,
                                            .inverse_radius_top = 0,
                                            .inverse_radius_bottom = 0,
                                            .inverse_offset_top = 0,
                                            .inverse_offset_bottom = 0};

// 保留兼容性的通用 Furious（右眼）
constexpr EyeConfig Preset_Furious = Preset_Furious_Right;

// Scared: 害怕，大眼+担忧眉
constexpr EyeConfig Preset_Scared = {.offset_x = 0,
                                     .offset_y = 0,
                                     .height = 85,
                                     .width = 70,
                                     .slope_top = -0.35f, // 担忧眉
                                     .slope_bottom = 0,
                                     .radius_top = 22,
                                     .radius_bottom = 15,
                                     .inverse_radius_top = 0,
                                     .inverse_radius_bottom = 0,
                                     .inverse_offset_top = 0,
                                     .inverse_offset_bottom = 0};

// Awe: 敬畏/惊叹，大圆眼
constexpr EyeConfig Preset_Awe = {.offset_x = 0,
                                  .offset_y = 0,
                                  .height = 90,
                                  .width = 85,
                                  .slope_top = -0.15f,
                                  .slope_bottom = 0.1f,
                                  .radius_top = 35,
                                  .radius_bottom = 30,
                                  .inverse_radius_top = 0,
                                  .inverse_radius_bottom = 0,
                                  .inverse_offset_top = 0,
                                  .inverse_offset_bottom = 0};

/**
 * @brief 根据表情枚举获取预设（通用，用于右眼或对称表情）
 */
inline const EyeConfig &GetPreset(Emotion emotion) {
  switch (emotion) {
  case Emotion::Normal:
    return Preset_Normal;
  case Emotion::Happy:
    return Preset_Happy;
  case Emotion::Glee:
    return Preset_Glee;
  case Emotion::Sad:
    return Preset_Sad;
  case Emotion::Worried:
    return Preset_Worried;
  case Emotion::Focused:
    return Preset_Focused;
  case Emotion::Annoyed:
    return Preset_Annoyed;
  case Emotion::Surprised:
    return Preset_Surprised;
  case Emotion::Skeptic:
    return Preset_Skeptic_Right; // 右眼
  case Emotion::Frustrated:
    return Preset_Frustrated;
  case Emotion::Unimpressed:
    return Preset_Unimpressed;
  case Emotion::Sleepy:
    return Preset_Sleepy;
  case Emotion::Suspicious:
    return Preset_Suspicious;
  case Emotion::Squint:
    return Preset_Squint;
  case Emotion::Angry:
    return Preset_Angry;
  case Emotion::Furious:
    return Preset_Furious;
  case Emotion::Scared:
    return Preset_Scared;
  case Emotion::Awe:
    return Preset_Awe;
  default:
    return Preset_Normal;
  }
}

/**
 * @brief 获取左眼预设（处理不对称表情）
 */
inline const EyeConfig &GetPresetLeft(Emotion emotion) {
  switch (emotion) {
  case Emotion::Skeptic:
    return Preset_Skeptic_Left; // 左眼不同！
  case Emotion::Furious:
    return Preset_Furious_Left; // 左眼位置更低
  default:
    return GetPreset(emotion); // 其他对称
  }
}

/**
 * @brief 获取右眼预设
 */
inline const EyeConfig &GetPresetRight(Emotion emotion) {
  return GetPreset(emotion); // 右眼走通用
}

/**
 * @brief 检查表情是否需要不对称处理
 */
inline bool IsAsymmetricEmotion(Emotion emotion) {
  return emotion == Emotion::Skeptic || emotion == Emotion::Furious;
}

} // namespace vector_eyes
