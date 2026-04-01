#ifndef QMI8658_SENSOR_H
#define QMI8658_SENSOR_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>

#include "i2c_device.h"

class Qmi8658Sensor : public I2cDevice {
public:
    using ShakeCallback = std::function<void()>;

    Qmi8658Sensor(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

    bool Initialize();
    bool Start();
    void SetShakeCallback(ShakeCallback callback);

private:
    struct Sample {
        float ax;
        float ay;
        float az;
        float gx;
        float gy;
        float gz;
    };

    struct MotionMetrics {
        float linear_accel;
        float jerk;
        float max_gyro;
        bool strong_accel;
        bool strong_gyro;
        bool trigger_now;
    };

    static void PollTask(void* arg);
    void PollLoop();
    bool Reset();
    void Configure();
    Sample ReadSample();
    MotionMetrics CalculateMotionMetrics(const Sample& sample);
    bool IsShakeGesture(const MotionMetrics& metrics);
    float MaxAbs3(float x, float y, float z) const;

    ShakeCallback shake_callback_;
    TaskHandle_t task_handle_ = nullptr;
    int strong_motion_score_ = 0;
    int settle_samples_ = 0;
    int64_t last_shake_time_us_ = 0;
    int64_t last_motion_log_time_us_ = 0;
    Sample gravity_estimate_ = {};
    Sample previous_sample_ = {};
    bool has_previous_sample_ = false;
};

#endif // QMI8658_SENSOR_H
