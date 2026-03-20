/**
 * @file vector_face.cc
 * @brief 双眼管理类实现
 */

#include "vector_face.h"
#include "eye_drawer.h"
#include "eye_presets.h"

namespace vector_eyes {

VectorFace::VectorFace(uint16_t screen_width, uint16_t screen_height,
                       uint16_t eye_size)
    : screen_width_(screen_width), screen_height_(screen_height),
      eye_size_(eye_size) {

  // 左眼需要镜像
  left_eye_.SetMirrored(true);
  right_eye_.SetMirrored(false);

  // 初始化眼睛位置
  UpdateEyePositions();

  // 设置初始表情
  left_eye_.ApplyPreset(Preset_Normal);
  right_eye_.ApplyPreset(Preset_Normal);
}

void VectorFace::SetCanvas(lv_obj_t *canvas) {
  canvas_ = canvas;
  EyeDrawer::SetCanvas(canvas);
}

void VectorFace::SetExpression(Emotion emotion) {
  // 检查是否需要不对称处理
  if (IsAsymmetricEmotion(emotion)) {
    // 左右眼使用不同的预设
    const EyeConfig &left_preset = GetPresetLeft(emotion);
    const EyeConfig &right_preset = GetPresetRight(emotion);
    left_eye_.TransitionTo(left_preset, 300);
    right_eye_.TransitionTo(right_preset, 300);
  } else {
    // 对称表情
    const EyeConfig &preset = GetPreset(emotion);
    SetExpression(preset);
  }
}

void VectorFace::SetExpression(const EyeConfig &preset) {
  left_eye_.TransitionTo(preset, 300);
  right_eye_.TransitionTo(preset, 300);
}

void VectorFace::Update() {
  // 更新眨眼
  blink_controller_.Update();
  float blink = blink_controller_.GetBlinkFactor();
  left_eye_.ApplyBlink(blink);
  right_eye_.ApplyBlink(blink);

  // 更新视线
  look_controller_.Update();
  float look_x = look_controller_.GetLookX();
  float look_y = look_controller_.GetLookY();
  left_eye_.ApplyLook(look_x, look_y);
  right_eye_.ApplyLook(look_x, look_y);

  // 更新眼睛动画
  left_eye_.Update();
  right_eye_.Update();
}

void VectorFace::Draw() {
  if (!canvas_)
    return;

  // 清空画布
  EyeDrawer::Clear(bg_color_);
  EyeDrawer::SetColor(eye_color_);

  // 绘制左眼
  EyeDrawer::Draw(left_eye_.GetCenterX(), left_eye_.GetCenterY(),
                  left_eye_.GetCurrentConfig());

  // 绘制右眼
  EyeDrawer::Draw(right_eye_.GetCenterX(), right_eye_.GetCenterY(),
                  right_eye_.GetCurrentConfig());
}

void VectorFace::SetRandomBehavior(bool blink, bool look) {
  blink_controller_.SetAutoBlinkEnabled(blink);
  look_controller_.SetRandomLookEnabled(look);
}

void VectorFace::UpdateEyePositions() {
  int16_t center_x = screen_width_ / 2;
  int16_t center_y = screen_height_ / 2;

  // 左眼在左边，右眼在右边
  left_eye_.SetCenter(center_x - eye_size_ / 2 - eye_inter_distance_, center_y);
  right_eye_.SetCenter(center_x + eye_size_ / 2 + eye_inter_distance_,
                       center_y);
}

} // namespace vector_eyes
