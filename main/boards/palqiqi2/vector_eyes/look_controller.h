/**
 * @file look_controller.h
 * @brief 视线控制器
 */

#pragma once

#include "utils.h"

namespace vector_eyes {

/**
 * @brief 视线控制器
 */
class LookController {
public:
    LookController();

    /**
     * @brief 更新视线状态
     */
    void Update();

    /**
     * @brief 看向指定方向
     * @param x X方向 (-1.0=左, 0=中, 1.0=右)
     * @param y Y方向 (-1.0=上, 0=中, 1.0=下)
     */
    void LookAt(float x, float y);

    /**
     * @brief 看向前方
     */
    void LookFront() { LookAt(0.0f, 0.0f); }

    /**
     * @brief 看向左边
     */
    void LookLeft() { LookAt(-1.0f, 0.0f); }

    /**
     * @brief 看向右边
     */
    void LookRight() { LookAt(1.0f, 0.0f); }

    /**
     * @brief 看向上方
     */
    void LookUp() { LookAt(0.0f, -1.0f); }

    /**
     * @brief 看向下方
     */
    void LookDown() { LookAt(0.0f, 1.0f); }

    /**
     * @brief 获取当前视线X
     */
    float GetLookX() const { return current_x_; }

    /**
     * @brief 获取当前视线Y
     */
    float GetLookY() const { return current_y_; }

    /**
     * @brief 启用/禁用随机视线
     */
    void SetRandomLookEnabled(bool enabled) { random_look_enabled_ = enabled; }

    /**
     * @brief 设置随机视线间隔范围（毫秒）
     */
    void SetLookInterval(uint32_t min_ms, uint32_t max_ms);

private:
    void ScheduleNextLook();
    void StartRandomLook();

    bool random_look_enabled_ = true;
    
    float current_x_ = 0.0f;
    float current_y_ = 0.0f;
    float target_x_ = 0.0f;
    float target_y_ = 0.0f;
    float start_x_ = 0.0f;
    float start_y_ = 0.0f;

    bool transitioning_ = false;
    uint32_t transition_start_ = 0;
    uint32_t transition_duration_ = 200;

    uint32_t last_look_ = 0;
    uint32_t next_look_interval_ = 2000;
    uint32_t min_interval_ = 1500;
    uint32_t max_interval_ = 4000;
};

} // namespace vector_eyes
