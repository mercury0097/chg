#ifndef TOUCH_HANDLER_H
#define TOUCH_HANDLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <driver/gpio.h>

#include <functional>

class TouchHandler {
public:
    using TouchCallback = std::function<void()>;

    TouchHandler();
    ~TouchHandler();

    // 启动触摸检测（返回ESP_OK表示成功）
    esp_err_t Start(TouchCallback callback);

    // 停止触摸检测
    void Stop();

private:
    TaskHandle_t task_handle_ = nullptr;
    TaskHandle_t callback_task_handle_ = nullptr;
    TouchCallback touch_callback_;
    volatile bool running_ = false;
    int64_t last_touch_time_ = 0;

    // GPIO中断相关
    gpio_num_t gpio_pin_ = GPIO_NUM_13;
    volatile bool interrupt_triggered_ = false;

    // GPIO中断处理函数（必须在IRAM中）
    static void IRAM_ATTR GpioIsrHandler(void* arg);
    
    // 触摸检测任务
    static void TouchDetectionTask(void* arg);
    static void TouchCallbackTask(void* arg);

    void DispatchTouchEvent();
};

#endif // TOUCH_HANDLER_H

