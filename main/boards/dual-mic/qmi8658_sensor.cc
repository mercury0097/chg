#include "qmi8658_sensor.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>
#include <esp_timer.h>

namespace {
constexpr const char* kTag = "Qmi8658Sensor";

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl3 = 0x04;
constexpr uint8_t kRegCtrl5 = 0x06;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegCtrl8 = 0x09;
constexpr uint8_t kRegAxL = 0x35;
constexpr uint8_t kRegRstResult = 0x4D;
constexpr uint8_t kRegReset = 0x60;

constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kResetCommand = 0xB0;
constexpr uint8_t kResetDoneValue = 0x80;

constexpr uint8_t kCtrl2Accel8g125Hz = 0x26;
constexpr uint8_t kCtrl3Gyro512dps112Hz = 0x56;
constexpr uint8_t kCtrl5EnableLpf = 0x11;
constexpr uint8_t kCtrl7EnableAccelGyro = 0x03;
constexpr uint8_t kCtrl8Handshake = 0x80;
constexpr uint8_t kCtrl1AddrAutoIncrement = 0x40;

constexpr float kAccelScale8g = 8.0f / 32768.0f;
constexpr float kGyroScale512dps = 512.0f / 32768.0f;

constexpr TickType_t kPollInterval = pdMS_TO_TICKS(50);
constexpr int kSettleSampleCount = 10;
constexpr int64_t kShakeCooldownUs = 4'000'000;
constexpr int64_t kMotionLogIntervalUs = 500'000;
constexpr float kGravityAlpha = 0.90f;
constexpr float kLinearAccelThreshold = 0.40f;
constexpr float kLinearAccelTriggerThreshold = 0.68f;
constexpr float kJerkThreshold = 0.68f;
constexpr float kJerkTriggerThreshold = 1.05f;
constexpr float kGyroThresholdDps = 82.0f;
constexpr float kGyroTriggerThresholdDps = 138.0f;
constexpr float kHardLinearAccelThreshold = 1.12f;
} // namespace

Qmi8658Sensor::Qmi8658Sensor(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : I2cDevice(i2c_bus, addr) {}

bool Qmi8658Sensor::Initialize() {
    if (!Reset()) {
        ESP_LOGE(kTag, "Failed to reset QMI8658A");
        return false;
    }

    const uint8_t who_am_i = ReadReg(kRegWhoAmI);
    if (who_am_i != kWhoAmIValue) {
        ESP_LOGE(kTag, "Unexpected QMI8658A WHO_AM_I: 0x%02x", who_am_i);
        return false;
    }

    WriteReg(kRegCtrl8, kCtrl8Handshake);
    Configure();
    settle_samples_ = 0;
    strong_motion_score_ = 0;
    last_shake_time_us_ = 0;
    last_motion_log_time_us_ = 0;
    gravity_estimate_ = {};
    previous_sample_ = {};
    has_previous_sample_ = false;

    ESP_LOGI(kTag, "QMI8658A initialized");
    return true;
}

bool Qmi8658Sensor::Start() {
    if (task_handle_ != nullptr) {
        return true;
    }

    const BaseType_t ret =
        xTaskCreate(PollTask, "qmi8658_poll", 4096, this, 5, &task_handle_);
    if (ret != pdPASS) {
        task_handle_ = nullptr;
        ESP_LOGE(kTag, "Failed to create QMI8658A polling task");
        return false;
    }

    return true;
}

void Qmi8658Sensor::SetShakeCallback(ShakeCallback callback) {
    shake_callback_ = callback;
}

void Qmi8658Sensor::PollTask(void* arg) {
    static_cast<Qmi8658Sensor*>(arg)->PollLoop();
}

void Qmi8658Sensor::PollLoop() {
    while (true) {
        const Sample sample = ReadSample();
        const MotionMetrics metrics = CalculateMotionMetrics(sample);
        if (!metrics.trigger_now &&
            (metrics.strong_accel || metrics.strong_gyro)) {
            const int64_t now_us = esp_timer_get_time();
            if (now_us - last_motion_log_time_us_ >= kMotionLogIntervalUs) {
                last_motion_log_time_us_ = now_us;
                ESP_LOGI(
                    kTag,
                    "Motion candidate: linear=%.2f jerk=%.2f gyro=%.1f accel=%d gyro=%d score=%d",
                    metrics.linear_accel, metrics.jerk, metrics.max_gyro,
                    metrics.strong_accel, metrics.strong_gyro,
                    strong_motion_score_);
            }
        }
        if (IsShakeGesture(metrics) && shake_callback_) {
            ESP_LOGI(kTag,
                     "Shake detected: accel=(%.2f, %.2f, %.2f) gyro=(%.1f, %.1f, %.1f) linear=%.2f jerk=%.2f",
                     sample.ax, sample.ay, sample.az, sample.gx, sample.gy,
                     sample.gz, metrics.linear_accel, metrics.jerk);
            shake_callback_();
        }
        vTaskDelay(kPollInterval);
    }
}

bool Qmi8658Sensor::Reset() {
    WriteReg(kRegReset, kResetCommand);

    for (int attempt = 0; attempt < 10; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (ReadReg(kRegRstResult) == kResetDoneValue) {
            WriteReg(kRegCtrl1, kCtrl1AddrAutoIncrement);
            return true;
        }
    }

    WriteReg(kRegCtrl1, kCtrl1AddrAutoIncrement);
    return false;
}

void Qmi8658Sensor::Configure() {
    WriteReg(kRegCtrl7, 0x00);
    WriteReg(kRegCtrl2, kCtrl2Accel8g125Hz);
    WriteReg(kRegCtrl3, kCtrl3Gyro512dps112Hz);
    WriteReg(kRegCtrl5, kCtrl5EnableLpf);
    WriteReg(kRegCtrl7, kCtrl7EnableAccelGyro);
}

Qmi8658Sensor::Sample Qmi8658Sensor::ReadSample() {
    uint8_t raw[12] = {0};
    ReadRegs(kRegAxL, raw, sizeof(raw));

    auto to_int16 = [&raw](size_t offset) -> int16_t {
        return static_cast<int16_t>(static_cast<uint16_t>(raw[offset]) |
                                    (static_cast<uint16_t>(raw[offset + 1]) << 8));
    };

    Sample sample = {};
    sample.ax = static_cast<float>(to_int16(0)) * kAccelScale8g;
    sample.ay = static_cast<float>(to_int16(2)) * kAccelScale8g;
    sample.az = static_cast<float>(to_int16(4)) * kAccelScale8g;
    sample.gx = static_cast<float>(to_int16(6)) * kGyroScale512dps;
    sample.gy = static_cast<float>(to_int16(8)) * kGyroScale512dps;
    sample.gz = static_cast<float>(to_int16(10)) * kGyroScale512dps;
    return sample;
}

Qmi8658Sensor::MotionMetrics Qmi8658Sensor::CalculateMotionMetrics(
    const Sample& sample) {
    MotionMetrics metrics = {};

    if (settle_samples_ < kSettleSampleCount) {
        gravity_estimate_ = sample;
        previous_sample_ = sample;
        has_previous_sample_ = true;
        ++settle_samples_;
        return metrics;
    }

    gravity_estimate_.ax =
        gravity_estimate_.ax * kGravityAlpha + sample.ax * (1.0f - kGravityAlpha);
    gravity_estimate_.ay =
        gravity_estimate_.ay * kGravityAlpha + sample.ay * (1.0f - kGravityAlpha);
    gravity_estimate_.az =
        gravity_estimate_.az * kGravityAlpha + sample.az * (1.0f - kGravityAlpha);

    const float linear_ax = sample.ax - gravity_estimate_.ax;
    const float linear_ay = sample.ay - gravity_estimate_.ay;
    const float linear_az = sample.az - gravity_estimate_.az;
    metrics.linear_accel =
        std::sqrt(linear_ax * linear_ax + linear_ay * linear_ay +
                  linear_az * linear_az);

    if (has_previous_sample_) {
        metrics.jerk = MaxAbs3(sample.ax - previous_sample_.ax,
                               sample.ay - previous_sample_.ay,
                               sample.az - previous_sample_.az);
    }
    previous_sample_ = sample;
    has_previous_sample_ = true;

    metrics.max_gyro = MaxAbs3(sample.gx, sample.gy, sample.gz);
    metrics.strong_accel =
        metrics.linear_accel > kLinearAccelThreshold ||
        metrics.jerk > kJerkThreshold;
    metrics.strong_gyro = metrics.max_gyro > kGyroThresholdDps;
    metrics.trigger_now =
        (metrics.linear_accel > kLinearAccelTriggerThreshold &&
         metrics.strong_gyro) ||
        (metrics.jerk > kJerkTriggerThreshold &&
         metrics.max_gyro > kGyroTriggerThresholdDps) ||
        metrics.linear_accel > kHardLinearAccelThreshold ||
        (metrics.strong_accel && metrics.max_gyro > kGyroTriggerThresholdDps);
    return metrics;
}

bool Qmi8658Sensor::IsShakeGesture(const MotionMetrics& metrics) {
    if (settle_samples_ < kSettleSampleCount) {
        return false;
    }

    if (metrics.trigger_now) {
        strong_motion_score_ = std::min(strong_motion_score_ + 2, 6);
    } else if (metrics.strong_accel || metrics.strong_gyro) {
        strong_motion_score_ = std::min(strong_motion_score_ + 1, 6);
    } else {
        strong_motion_score_ = std::max(strong_motion_score_ - 2, 0);
    }

    if (strong_motion_score_ < 2) {
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    if (now_us - last_shake_time_us_ < kShakeCooldownUs) {
        return false;
    }

    strong_motion_score_ = 0;
    last_shake_time_us_ = now_us;
    return true;
}

float Qmi8658Sensor::MaxAbs3(float x, float y, float z) const {
    return std::max({std::fabs(x), std::fabs(y), std::fabs(z)});
}
