/**
 * @file look_controller.cc
 * @brief 视线控制器实现
 */

#include "look_controller.h"

namespace vector_eyes {

LookController::LookController() {
    last_look_ = millis_idf();
    ScheduleNextLook();
}

void LookController::Update() {
    uint32_t now = millis_idf();

    // 更新视线过渡
    if (transitioning_) {
        uint32_t elapsed = now - transition_start_;
        float t = static_cast<float>(elapsed) / transition_duration_;

        if (t >= 1.0f) {
            t = 1.0f;
            transitioning_ = false;
        }

        t = EaseOutQuad(t);
        current_x_ = Lerp(start_x_, target_x_, t);
        current_y_ = Lerp(start_y_, target_y_, t);
    }

    // 检查是否需要随机视线
    if (random_look_enabled_ && !transitioning_ && 
        now - last_look_ > next_look_interval_) {
        StartRandomLook();
    }
}

void LookController::LookAt(float x, float y) {
    start_x_ = current_x_;
    start_y_ = current_y_;
    target_x_ = Clamp(x, -1.0f, 1.0f);
    target_y_ = Clamp(y, -1.0f, 1.0f);
    transition_start_ = millis_idf();
    transitioning_ = true;
    last_look_ = millis_idf();
    ScheduleNextLook();
}

void LookController::StartRandomLook() {
    // 随机选择一个方向，但大部分时间看前方
    float rand_val = RandomFloat();
    
    float new_x, new_y;
    
    if (rand_val < 0.4f) {
        // 40% 概率看前方
        new_x = 0.0f;
        new_y = 0.0f;
    } else if (rand_val < 0.6f) {
        // 20% 概率看左
        new_x = -0.5f - RandomFloat() * 0.5f;
        new_y = (RandomFloat() - 0.5f) * 0.3f;
    } else if (rand_val < 0.8f) {
        // 20% 概率看右
        new_x = 0.5f + RandomFloat() * 0.5f;
        new_y = (RandomFloat() - 0.5f) * 0.3f;
    } else {
        // 20% 概率随机方向
        new_x = (RandomFloat() - 0.5f) * 1.5f;
        new_y = (RandomFloat() - 0.5f) * 0.8f;
    }

    LookAt(new_x, new_y);
}

void LookController::ScheduleNextLook() {
    next_look_interval_ = RandomInt(min_interval_, max_interval_);
}

void LookController::SetLookInterval(uint32_t min_ms, uint32_t max_ms) {
    min_interval_ = min_ms;
    max_interval_ = max_ms;
    ScheduleNextLook();
}

} // namespace vector_eyes
