/**
 * @file emotions.h
 * @brief 表情枚举定义
 */

#pragma once

namespace vector_eyes {

/**
 * @brief 支持的表情类型枚举
 */
enum class Emotion {
    Normal = 0,
    Angry,
    Glee,
    Happy,
    Sad,
    Worried,
    Focused,
    Annoyed,
    Surprised,
    Skeptic,
    Frustrated,
    Unimpressed,
    Sleepy,
    Suspicious,
    Squint,
    Furious,
    Scared,
    Awe,
    COUNT  // 表情总数
};

} // namespace vector_eyes
