#include "touch_handler.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace {
constexpr const char* kTag = "TouchHandler";
constexpr gpio_num_t kTouchGpio = GPIO_NUM_13;
constexpr int64_t kDebounceTimeUs = 2'000'000;  // 2秒防抖
constexpr int kDebounceSampleDelayMs = 50;       // 中断后延迟50ms再采样
}

TouchHandler::TouchHandler() = default;

TouchHandler::~TouchHandler() {
    Stop();
}

void IRAM_ATTR TouchHandler::GpioIsrHandler(void* arg) {
    auto* handler = static_cast<TouchHandler*>(arg);
    
    // 设置中断标志
    handler->interrupt_triggered_ = true;
    
    // 通知检测任务（从ISR中唤醒）
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (handler->task_handle_ != nullptr) {
        vTaskNotifyGiveFromISR(handler->task_handle_, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void TouchHandler::TouchDetectionTask(void* arg) {
    auto* handler = static_cast<TouchHandler*>(arg);

    ESP_LOGI(kTag, "Touch detection task started on CPU%d (GPIO interrupt mode)", xPortGetCoreID());

    while (handler->running_) {
        // 等待GPIO中断通知（阻塞直到中断触发）
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!handler->running_) {
            break;
        }

        // 中断触发后，延迟50ms再次采样（软件防抖）
        vTaskDelay(pdMS_TO_TICKS(kDebounceSampleDelayMs));

        // 确认GPIO仍然是高电平（人体仍在感应范围内）
        int gpio_level = gpio_get_level(handler->gpio_pin_);
        if (gpio_level == 1) {
            // 检查距离上次触发是否超过防抖时间（2秒）
            const int64_t now = esp_timer_get_time();
            if (now - handler->last_touch_time_ >= kDebounceTimeUs) {
                handler->last_touch_time_ = now;
                
                ESP_LOGI(kTag, "✅ Human detected via GPIO interrupt!");
                
                // 分发触摸事件
                handler->DispatchTouchEvent();
            } else {
                ESP_LOGD(kTag, "Touch ignored (within debounce period)");
            }
        } else {
            ESP_LOGD(kTag, "False trigger: GPIO returned to LOW after debounce delay");
        }

        // 清除中断标志
        handler->interrupt_triggered_ = false;
    }

    ESP_LOGI(kTag, "Touch detection task stopped");
    vTaskDelete(nullptr);
}

void TouchHandler::TouchCallbackTask(void* arg) {
    auto* handler = static_cast<TouchHandler*>(arg);
    ESP_LOGI(kTag, "Touch callback dispatcher started on CPU%d", xPortGetCoreID());

    while (handler->running_) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!handler->running_) {
            break;
        }

        if (handler->touch_callback_) {
            handler->touch_callback_();
        }
    }

    ESP_LOGI(kTag, "Touch callback dispatcher stopped");
    vTaskDelete(nullptr);
}

void TouchHandler::DispatchTouchEvent() {
    if (callback_task_handle_ != nullptr) {
        xTaskNotifyGive(callback_task_handle_);
    } else if (touch_callback_) {
        // Fallback: invoke directly if dispatcher not available
        touch_callback_();
    }
}

esp_err_t TouchHandler::Start(TouchCallback callback) {
    if (running_) {
        ESP_LOGW(kTag, "Touch handler already running");
        return ESP_OK;
    }

    touch_callback_ = std::move(callback);
    running_ = true;

    // 配置GPIO13为输入模式，带下拉电阻（默认低电平）
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << gpio_pin_);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;  // 默认拉低
    io_conf.intr_type = GPIO_INTR_POSEDGE;        // 上升沿触发（检测到人体时3.3V高电平）

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "gpio_config failed: %s", esp_err_to_name(ret));
        running_ = false;
        return ret;
    }

    // 安装GPIO中断服务
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means service already installed (OK)
        ESP_LOGE(kTag, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        running_ = false;
        return ret;
    }

    // 添加GPIO中断处理函数
    ret = gpio_isr_handler_add(gpio_pin_, GpioIsrHandler, this);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        running_ = false;
        return ret;
    }

    int initial_level = gpio_get_level(gpio_pin_);
    ESP_LOGI(kTag, "✅ GPIO13 configured as input with pull-down (external sensor mode)");
    ESP_LOGI(kTag, "   📌 Trigger: POSEDGE (3.3V HIGH when human detected)");
    ESP_LOGI(kTag, "   🔌 Wiring: Sensor-OUT → GPIO13, Sensor-VCC → 3.3V, Sensor-GND → GND");
    ESP_LOGI(kTag, "   📊 Initial GPIO level: %s", initial_level ? "HIGH" : "LOW");

    // 创建回调分发任务
    BaseType_t callback_task_ret = xTaskCreatePinnedToCore(
        TouchCallbackTask,
        "touch_cb",
        4096,
        this,
        3,
        &callback_task_handle_,
        1
    );

    if (callback_task_ret != pdPASS) {
        ESP_LOGW(kTag, "Failed to create touch callback dispatcher task, fallback to direct callback execution");
        callback_task_handle_ = nullptr;
    }

    // 创建检测任务
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        TouchDetectionTask,
        "touch_detect",
        4096,
        this,
        3,
        &task_handle_,
        1
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(kTag, "Failed to create touch detection task");
        if (callback_task_handle_ != nullptr) {
            vTaskDelete(callback_task_handle_);
            callback_task_handle_ = nullptr;
        }
        gpio_isr_handler_remove(gpio_pin_);
        running_ = false;
        return ESP_FAIL;
    }

    ESP_LOGI(kTag, "Touch handler started successfully on CPU1 (GPIO13)");
    return ESP_OK;
}

void TouchHandler::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // 通知任务退出
    if (task_handle_ != nullptr) {
        xTaskNotifyGive(task_handle_);
    }
    if (callback_task_handle_ != nullptr) {
        xTaskNotifyGive(callback_task_handle_);
    }

    // 等待任务结束
    vTaskDelay(pdMS_TO_TICKS(100));

    if (callback_task_handle_ != nullptr) {
        vTaskDelete(callback_task_handle_);
        callback_task_handle_ = nullptr;
    }

    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }

    // 清理GPIO中断
    gpio_isr_handler_remove(gpio_pin_);
    
    ESP_LOGI(kTag, "Touch handler stopped");
}
