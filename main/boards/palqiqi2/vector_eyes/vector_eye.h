/**
 * @file vector_eye.h
 * @brief 单眼类
 */

#pragma once

#include "eye_config.h"
#include "eye_presets.h"
#include "utils.h"

namespace vector_eyes {

/**
 * @brief 单眼类，管理一只眼睛的状态和动画
 */
class VectorEye {
public:
    VectorEye();

    /**
     * @brief 设置眼睛中心位置
     */
    void SetCenter(int16_t x, int16_t y);

    /**
     * @brief 设置是否镜像（左眼需要镜像）
     */
    void SetMirrored(bool mirrored);

    /**
     * @brief 立即应用预设
     */
    void ApplyPreset(const EyeConfig& preset);

    /**
     * @brief 平滑过渡到目标预设
     */
    void TransitionTo(const EyeConfig& target, uint32_t duration_ms = 300);

    /**
     * @brief 更新动画状态
     */
    void Update();

    /**
     * @brief 获取当前配置（用于绘制）
     */
    EyeConfig* GetCurrentConfig();

    /**
     * @brief 获取眼睛中心X
     */
    int16_t GetCenterX() const { return center_x_; }

    /**
     * @brief 获取眼睛中心Y
     */
    int16_t GetCenterY() const { return center_y_; }

    /**
     * @brief 应用眨眼效果
     * @param blink_factor 眨眼程度 0.0=睁眼, 1.0=闭眼
     */
    void ApplyBlink(float blink_factor);

    /**
     * @brief 应用视线偏移
     * @param look_x X方向偏移 (-1.0 ~ 1.0)
     * @param look_y Y方向偏移 (-1.0 ~ 1.0)
     */
    void ApplyLook(float look_x, float look_y);

private:
    int16_t center_x_ = 0;
    int16_t center_y_ = 0;
    bool is_mirrored_ = false;

    EyeConfig base_config_;      // 基础配置（表情）
    EyeConfig current_config_;   // 当前配置（含动画效果）
    EyeConfig start_config_;     // 过渡起始配置
    EyeConfig target_config_;    // 过渡目标配置

    bool transitioning_ = false;
    uint32_t transition_start_ = 0;
    uint32_t transition_duration_ = 300;

    float blink_factor_ = 0.0f;
    float look_x_ = 0.0f;
    float look_y_ = 0.0f;

    void UpdateTransition();
    void ApplyEffects();
};

} // namespace vector_eyes
