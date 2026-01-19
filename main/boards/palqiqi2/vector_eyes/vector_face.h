/**
 * @file vector_face.h
 * @brief 双眼管理类
 */

#pragma once

#include "blink_controller.h"
#include "emotions.h"
#include "look_controller.h"
#include "vector_eye.h"
#include <lvgl.h>

namespace vector_eyes {

/**
 * @brief 双眼管理类
 */
class VectorFace {
public:
  /**
   * @brief 构造函数
   * @param screen_width 屏幕宽度
   * @param screen_height 屏幕高度
   * @param eye_size 眼睛大小
   */
  VectorFace(uint16_t screen_width, uint16_t screen_height, uint16_t eye_size);

  /**
   * @brief 设置绘图 canvas
   */
  void SetCanvas(lv_obj_t *canvas);

  /**
   * @brief 设置表情
   */
  void SetExpression(Emotion emotion);

  /**
   * @brief 设置表情（通过预设）
   */
  void SetExpression(const EyeConfig &preset);

  /**
   * @brief 更新状态（每帧调用）
   */
  void Update();

  /**
   * @brief 绘制眼睛
   */
  void Draw();

  /**
   * @brief 手动触发眨眼
   */
  void Blink() { blink_controller_.Blink(); }

  /**
   * @brief 看向指定方向
   */
  void LookAt(float x, float y) { look_controller_.LookAt(x, y); }

  /**
   * @brief 启用/禁用随机行为
   */
  void SetRandomBehavior(bool blink, bool look);

  /**
   * @brief 设置眼睛颜色
   */
  void SetEyeColor(lv_color_t color) { eye_color_ = color; }

  /**
   * @brief 设置背景颜色
   */
  void SetBackgroundColor(lv_color_t color) { bg_color_ = color; }

  /**
   * @brief 获取眨眼控制器
   */
  BlinkController &GetBlinkController() { return blink_controller_; }

  /**
   * @brief 获取视线控制器
   */
  LookController &GetLookController() { return look_controller_; }

private:
  uint16_t screen_width_;
  uint16_t screen_height_;
  uint16_t eye_size_;
  uint16_t eye_inter_distance_ = 20; // 两眼间距（Cozmo风格，适当分开）

  VectorEye left_eye_;
  VectorEye right_eye_;
  BlinkController blink_controller_;
  LookController look_controller_;

  lv_obj_t *canvas_ = nullptr;
  lv_color_t eye_color_ = lv_color_white();
  lv_color_t bg_color_ = lv_color_black();

  void UpdateEyePositions();
};

} // namespace vector_eyes
