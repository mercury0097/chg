/**
 * @file vector_eye.cc
 * @brief 单眼类实现
 */

#include "vector_eye.h"
#include <algorithm>

namespace vector_eyes {

VectorEye::VectorEye() {
  base_config_ = Preset_Normal;
  current_config_ = base_config_;
}

void VectorEye::SetCenter(int16_t x, int16_t y) {
  center_x_ = x;
  center_y_ = y;
}

void VectorEye::SetMirrored(bool mirrored) { is_mirrored_ = mirrored; }

void VectorEye::ApplyPreset(const EyeConfig &preset) {
  base_config_ = preset;
  current_config_ = preset;
  transitioning_ = false;
}

void VectorEye::TransitionTo(const EyeConfig &target, uint32_t duration_ms) {
  start_config_ = base_config_;
  target_config_ = target;
  transition_start_ = millis_idf();
  transition_duration_ = duration_ms;
  transitioning_ = true;
}

void VectorEye::Update() {
  UpdateTransition();
  ApplyEffects();
}

void VectorEye::UpdateTransition() {
  if (!transitioning_)
    return;

  uint32_t elapsed = millis_idf() - transition_start_;
  float t = static_cast<float>(elapsed) / transition_duration_;

  if (t >= 1.0f) {
    t = 1.0f;
    transitioning_ = false;
    base_config_ = target_config_;
  }

  // 使用缓动函数
  t = EaseInOutQuad(t);

  // 插值所有参数
  base_config_ = EyeConfig::Lerp(start_config_, target_config_, t);
}

void VectorEye::ApplyEffects() {
  current_config_ = base_config_;

  // 镜像眼睛（左眼）需要反转斜边方向
  // 这样可以正确形成"倒八字"愤怒眉效果：左眼削右上角，右眼削左上角
  if (is_mirrored_) {
    current_config_.slope_top = -current_config_.slope_top;
    current_config_.slope_bottom = -current_config_.slope_bottom;
  }

  // 应用眨眼效果：减少高度
  if (blink_factor_ > 0.0f) {
    current_config_.height = static_cast<int16_t>(
        base_config_.height * (1.0f - blink_factor_ * 0.95f));
    // 眨眼时眼睛向下移动一点
    current_config_.offset_y +=
        static_cast<int16_t>(base_config_.height * blink_factor_ * 0.3f);
  }

  // 应用视线偏移
  int16_t max_look_offset_x = 10; // X方向最大偏移（减小防止重叠）
  int16_t max_look_offset_y = 12; // Y方向最大偏移

  // 镜像时 X 方向反转
  float effective_look_x = is_mirrored_ ? -look_x_ : look_x_;

  current_config_.offset_x +=
      static_cast<int16_t>(effective_look_x * max_look_offset_x);
  current_config_.offset_y += static_cast<int16_t>(look_y_ * max_look_offset_y);
}

EyeConfig *VectorEye::GetCurrentConfig() { return &current_config_; }

void VectorEye::ApplyBlink(float blink_factor) {
  blink_factor_ = Clamp(blink_factor, 0.0f, 1.0f);
}

void VectorEye::ApplyLook(float look_x, float look_y) {
  look_x_ = Clamp(look_x, -1.0f, 1.0f);
  look_y_ = Clamp(look_y, -1.0f, 1.0f);
}

} // namespace vector_eyes
