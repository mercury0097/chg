#ifndef __OSCILLATOR_SMOOTH_H__
#define __OSCILLATOR_SMOOTH_H__

#include <stdint.h>

#define M_PI 3.14159265358979323846

#ifndef DEG2RAD
#define DEG2RAD(g) ((g) * M_PI) / 180
#endif

// 优化的舵机参数
#define SERVO_MIN_PULSEWIDTH_US 500           // 最小脉宽（微秒）
#define SERVO_MAX_PULSEWIDTH_US 2500          // 最大脉宽（微秒）
#define SERVO_MIN_DEGREE -90                  // 最小角度
#define SERVO_MAX_DEGREE 90                   // 最大角度
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000  // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD 20000           // 20000 ticks, 20ms

// 平滑度等级
enum SmoothLevel {
    SMOOTH_LEVEL_NONE = 0,     // 无平滑
    SMOOTH_LEVEL_LOW = 1,      // 低平滑度
    SMOOTH_LEVEL_MEDIUM = 2,   // 中等平滑度（推荐）
    SMOOTH_LEVEL_HIGH = 3      // 高平滑度
};

class Oscillator {
public:
    Oscillator(int trim = 0);
    ~Oscillator();
    void Attach(int pin, bool rev = false);
    void Detach();

    void SetA(unsigned int amplitude) { amplitude_ = amplitude; };
    void SetO(int offset) { offset_ = offset; };
    void SetPh(double Ph) { phase0_ = Ph; };
    void SetT(unsigned int period);
    void SetTrim(int trim) { trim_ = trim; };
    void SetLimiter(int diff_limit) { diff_limit_ = diff_limit; };
    void DisableLimiter() { diff_limit_ = 0; };
    int GetTrim() { return trim_; };
    void SetPosition(int position);
    void Stop() { stop_ = true; };
    void Play() { stop_ = false; };
    void Reset() { phase_ = 0; };
    void Refresh();
    int GetPosition() { return pos_; }
    
    // 新增：平滑度控制
    void SetSmoothLevel(SmoothLevel level);
    
    // 新增：设置采样周期（默认15ms，更流畅）
    void SetSamplingPeriod(unsigned int period_ms) { sampling_period_ = period_ms; }

private:
    bool NextSample();
    void Write(int position);
    uint32_t AngleToCompare(int angle);
    
    // 新增：缓动函数（Ease In-Out）
    double EaseInOutCubic(double t);
    double EaseInOutQuad(double t);
    
    // 新增：S型曲线平滑函数
    double ApplySmoothing(double value);

private:
    bool is_attached_;

    //-- Oscillators parameters
    unsigned int amplitude_;  //-- Amplitude (degrees)
    int offset_;              //-- Offset (degrees)
    unsigned int period_;     //-- Period (milliseconds)
    double phase0_;           //-- Phase (radians)

    //-- Internal variables
    int pos_;                       //-- Current servo pos
    int target_pos_;                //-- Target position for smoothing
    int pin_;                       //-- Pin where the servo is connected
    int trim_;                      //-- Calibration offset
    double phase_;                  //-- Current phase
    double inc_;                    //-- Increment of phase
    double number_samples_;         //-- Number of samples
    unsigned int sampling_period_;  //-- sampling period (ms) - 默认15ms

    long previous_millis_;
    long current_millis_;

    //-- Oscillation mode. If true, the servo is stopped
    bool stop_;

    //-- Reverse mode
    bool rev_;

    int diff_limit_;
    long previous_servo_command_millis_;
    
    // 新增：平滑度参数
    SmoothLevel smooth_level_;
    double smoothing_factor_;  // 平滑因子 (0.0 - 1.0)

    int ledc_channel_;
    int ledc_speed_mode_;
};

#endif  // __OSCILLATOR_SMOOTH_H__
