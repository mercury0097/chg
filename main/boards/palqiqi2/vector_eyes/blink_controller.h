/**
 * @file blink_controller.h
 * @brief 眨眼控制器
 */

#pragma once

#include "utils.h"

namespace vector_eyes {

/**
 * @brief 眨眼控制器
 */
class BlinkController {
public:
    BlinkController();

    /**
     * @brief 更新眨眼状态
     */
    void Update();

    /**
     * @brief 手动触发眨眼
     */
    void Blink();

    /**
     * @brief 获取当前眨眼程度
     * @return 0.0=睁眼, 1.0=闭眼
     */
    float GetBlinkFactor() const { return blink_factor_; }

    /**
     * @brief 启用/禁用自动眨眼
     */
    void SetAutoBlinkEnabled(bool enabled) { auto_blink_enabled_ = enabled; }

    /**
     * @brief 设置眨眼间隔范围（毫秒）
     */
    void SetBlinkInterval(uint32_t min_ms, uint32_t max_ms);

private:
    void StartBlink();
    void ScheduleNextBlink();

    bool auto_blink_enabled_ = true;
    bool blinking_ = false;
    float blink_factor_ = 0.0f;

    uint32_t last_blink_ = 0;
    uint32_t blink_start_ = 0;
    uint32_t next_blink_interval_ = 3000;

    uint32_t min_interval_ = 2000;
    uint32_t max_interval_ = 6000;

    static constexpr uint32_t BLINK_DURATION = 150;  // 眨眼总时长
    static constexpr float CLOSE_RATIO = 0.35f;      // 闭眼阶段占比
};

} // namespace vector_eyes
