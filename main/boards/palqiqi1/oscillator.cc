#include "oscillator.h"

#include <driver/ledc.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>

static const char *TAG = "Oscillator";

// millis() 函数声明（实现在palqiqi_movements.cc中）
extern unsigned long millis();

static ledc_channel_t next_free_channel = LEDC_CHANNEL_0;

Oscillator::Oscillator(int trim) {
  trim_ = trim;
  diff_limit_ = 0;
  is_attached_ = false;

  // 提高采样频率，让动作更流畅 (v3优化)
  sampling_period_ = 20; // 20ms = 50Hz (原始:30ms = 33Hz)
  period_ = 2000;
  number_samples_ = (double)period_ / (double)sampling_period_;
  inc_ = 2 * M_PI / number_samples_;

  amplitude_ = 45;
  phase_ = 0;
  phase0_ = 0;
  offset_ = 0;
  stop_ = false;
  rev_ = false;

  pos_ = 90;
  target_pos_ = 90;
  previous_millis_ = 0;

  // 默认低平滑度，避免延迟过大
  smooth_level_ = SMOOTH_LEVEL_LOW;
  smoothing_factor_ = 0.5; // 平滑因子
}

Oscillator::~Oscillator() { Detach(); }

// 缓动函数：三次方缓入缓出（更平滑的加速/减速）
double Oscillator::EaseInOutCubic(double t) {
  if (t < 0.5) {
    return 4 * t * t * t;
  } else {
    double f = 2 * t - 2;
    return 0.5 * f * f * f + 1;
  }
}

// 缓动函数：二次方缓入缓出（较温和的平滑）
double Oscillator::EaseInOutQuad(double t) {
  if (t < 0.5) {
    return 2 * t * t;
  } else {
    return -1 + (4 - 2 * t) * t;
  }
}

// S型曲线平滑函数
double Oscillator::ApplySmoothing(double value) {
  if (smooth_level_ == SMOOTH_LEVEL_NONE) {
    return value;
  }

  // 使用平滑因子进行指数移动平均
  static double smoothed_value = value;
  smoothed_value =
      smoothed_value * (1.0 - smoothing_factor_) + value * smoothing_factor_;
  return smoothed_value;
}

void Oscillator::SetSmoothLevel(SmoothLevel level) {
  smooth_level_ = level;

  // 根据平滑度等级设置平滑因子
  switch (level) {
  case SMOOTH_LEVEL_NONE:
    smoothing_factor_ = 1.0; // 无平滑
    break;
  case SMOOTH_LEVEL_LOW:
    smoothing_factor_ = 0.5;
    break;
  case SMOOTH_LEVEL_MEDIUM:
    smoothing_factor_ = 0.3; // 推荐值
    break;
  case SMOOTH_LEVEL_HIGH:
    smoothing_factor_ = 0.15;
    break;
  }
}

// 优化的角度到脉宽转换（更精确）
uint32_t Oscillator::AngleToCompare(int angle) {
  // 确保角度在有效范围内
  angle = (angle > 180) ? 180 : ((angle < 0) ? 0 : angle);

  // 精确计算脉宽
  // 0度 = 500us, 90度 = 1500us, 180度 = 2500us
  uint32_t pulsewidth_us =
      SERVO_MIN_PULSEWIDTH_US +
      (angle * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US)) / 180;

  return pulsewidth_us;
}

bool Oscillator::NextSample() {
  current_millis_ = millis();

  if (current_millis_ - previous_millis_ >= sampling_period_) {
    previous_millis_ = current_millis_;
    return true;
  }

  return false;
}

void Oscillator::Attach(int pin, bool rev) {
  if (is_attached_) {
    Detach();
  }

  pin_ = pin;
  rev_ = rev;

  ledc_timer_config_t ledc_timer = {};
  ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
  ledc_timer.timer_num = LEDC_TIMER_1;
  ledc_timer.freq_hz = 50;
  ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  static int last_channel = 0;
  last_channel = (last_channel + 1) % 7 + 1;
  ledc_channel_ = last_channel;

  ledc_channel_config_t ledc_channel = {};
  ledc_channel.gpio_num = pin_;
  ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_channel.channel = (ledc_channel_t)ledc_channel_;
  ledc_channel.intr_type = LEDC_INTR_DISABLE;
  ledc_channel.timer_sel = LEDC_TIMER_1;
  ledc_channel.duty = 0;
  ledc_channel.hpoint = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

  ledc_speed_mode_ = LEDC_LOW_SPEED_MODE;

  previous_servo_command_millis_ = millis();

  is_attached_ = true;
}

void Oscillator::Detach() {
  if (!is_attached_)
    return;

  ESP_ERROR_CHECK(ledc_stop((ledc_mode_t)ledc_speed_mode_,
                            (ledc_channel_t)ledc_channel_, 0));

  is_attached_ = false;
}

void Oscillator::SetT(unsigned int T) {
  period_ = T;

  number_samples_ = (double)period_ / (double)sampling_period_;
  inc_ = 2 * M_PI / number_samples_;
}

void Oscillator::SetPosition(int position) { Write(position); }

void Oscillator::Refresh() {
  if (NextSample()) {
    if (!stop_) {
      // 直接计算位置，不使用过度平滑避免延迟
      int pos = (int)round(amplitude_ * sin(phase_ + phase0_) + offset_);
      if (rev_)
        pos = -pos;
      Write(pos + 90);
    }

    phase_ = phase_ + inc_;
  }
}

void Oscillator::Write(int position) {
  if (!is_attached_)
    return;

  long currentMillis = millis();

  // 平滑过渡到目标位置
  if (diff_limit_ > 0) {
    int elapsed = (int)(currentMillis - previous_servo_command_millis_);
    int limit = (elapsed * diff_limit_) / 1000;
    if (limit < 1)
      limit = 1;

    int diff = position - pos_;
    if (diff > limit) {
      pos_ += limit;
    } else if (diff < -limit) {
      pos_ -= limit;
    } else {
      pos_ = position;
    }
  } else {
    pos_ = position;
  }
  previous_servo_command_millis_ = currentMillis;

  int angle = pos_ + trim_;

  // v12优化：更严格的角度限制，避免舵机过载
  // Otto机器人的舵机安全范围约为 20-160 度
  // 超出此范围可能导致机械干涉和舵机堵转
  constexpr int SAFE_ANGLE_MIN = 15;
  constexpr int SAFE_ANGLE_MAX = 165;
  
  if (angle < SAFE_ANGLE_MIN)
    angle = SAFE_ANGLE_MIN;
  if (angle > SAFE_ANGLE_MAX)
    angle = SAFE_ANGLE_MAX;

  // 优化的脉宽计算
  uint32_t pulsewidth_us = AngleToCompare(angle);

  // 将脉宽转换为占空比
  // PWM周期 = 20ms = 20000us
  // duty = (pulsewidth_us / 20000) * 2^13
  uint32_t duty = (pulsewidth_us * 8191) / 20000;

  ESP_ERROR_CHECK(ledc_set_duty((ledc_mode_t)ledc_speed_mode_,
                                (ledc_channel_t)ledc_channel_, duty));
  ESP_ERROR_CHECK(ledc_update_duty((ledc_mode_t)ledc_speed_mode_,
                                   (ledc_channel_t)ledc_channel_));
}
