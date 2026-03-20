/**
 * @file blink_controller.cc
 * @brief 眨眼控制器实现
 */

#include "blink_controller.h"

namespace vector_eyes {

BlinkController::BlinkController() {
    last_blink_ = millis_idf();
    ScheduleNextBlink();
}

void BlinkController::Update() {
    uint32_t now = millis_idf();

    // 检查是否需要自动眨眼
    if (auto_blink_enabled_ && !blinking_ && 
        now - last_blink_ > next_blink_interval_) {
        StartBlink();
    }

    // 更新眨眼动画
    if (blinking_) {
        uint32_t elapsed = now - blink_start_;
        float t = static_cast<float>(elapsed) / BLINK_DURATION;

        if (t < CLOSE_RATIO) {
            // 闭眼阶段：快速闭眼
            blink_factor_ = EaseInQuad(t / CLOSE_RATIO);
        } else if (t < 1.0f) {
            // 睁眼阶段：稍慢睁眼
            float open_t = (t - CLOSE_RATIO) / (1.0f - CLOSE_RATIO);
            blink_factor_ = 1.0f - EaseOutQuad(open_t);
        } else {
            // 眨眼结束
            blinking_ = false;
            blink_factor_ = 0.0f;
            last_blink_ = now;
            ScheduleNextBlink();
        }
    }
}

void BlinkController::Blink() {
    if (!blinking_) {
        StartBlink();
    }
}

void BlinkController::StartBlink() {
    blinking_ = true;
    blink_start_ = millis_idf();
    blink_factor_ = 0.0f;
}

void BlinkController::ScheduleNextBlink() {
    next_blink_interval_ = RandomInt(min_interval_, max_interval_);
}

void BlinkController::SetBlinkInterval(uint32_t min_ms, uint32_t max_ms) {
    min_interval_ = min_ms;
    max_interval_ = max_ms;
    ScheduleNextBlink();
}

} // namespace vector_eyes
