/**
 * @file utils.h
 * @brief 工具函数：时间、缓动等
 */

#pragma once

#include <cstdint>
#include <esp_timer.h>
#include <cmath>

namespace vector_eyes {

/**
 * @brief 获取毫秒时间戳（替代 Arduino millis()）
 */
inline uint32_t millis_idf() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

/**
 * @brief 线性插值
 */
inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/**
 * @brief 整数线性插值
 */
inline int16_t LerpInt(int16_t a, int16_t b, float t) {
    return static_cast<int16_t>(a + (b - a) * t);
}

/**
 * @brief EaseInOutQuad 缓动函数
 * 开始和结束慢，中间快
 */
inline float EaseInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

/**
 * @brief EaseOutQuad 缓动函数
 * 开始快，结束慢
 */
inline float EaseOutQuad(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

/**
 * @brief EaseInQuad 缓动函数
 * 开始慢，结束快
 */
inline float EaseInQuad(float t) {
    return t * t;
}

/**
 * @brief 限制值在范围内
 */
template<typename T>
inline T Clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * @brief 简单随机数 (0.0 ~ 1.0)
 */
inline float RandomFloat() {
    return static_cast<float>(rand()) / RAND_MAX;
}

/**
 * @brief 范围内随机整数
 */
inline int RandomInt(int min_val, int max_val) {
    return min_val + rand() % (max_val - min_val + 1);
}

} // namespace vector_eyes
