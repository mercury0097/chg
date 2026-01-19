#include "event_bus.h"
#include <esp_log.h>
#include <cstring>

namespace xiaozhi {

static const char* TAG = "EventBus";

// ============================================================================
// 定义事件域
// ============================================================================

ESP_EVENT_DEFINE_BASE(PET_EVENT);
ESP_EVENT_DEFINE_BASE(EMO_EVENT);
ESP_EVENT_DEFINE_BASE(LOGIC_EVENT);
ESP_EVENT_DEFINE_BASE(CLOUD_EVENT);
ESP_EVENT_DEFINE_BASE(LEARNING_EVENT);

// ============================================================================
// EventBus 实现
// ============================================================================

EventBus& EventBus::GetInstance() {
  static EventBus instance;
  return instance;
}

esp_err_t EventBus::Initialize() {
  if (initialized_) {
    ESP_LOGW(TAG, "EventBus already initialized");
    return ESP_OK;
  }

  // 创建事件循环配置
  esp_event_loop_args_t loop_args = {
      .queue_size = 64,                // 队列大小（可根据实际调整）
      .task_name = "event_bus",        // 任务名称
      .task_priority = 5,              // 优先级（中等）
      .task_stack_size = 4096,         // 栈大小 4KB
      .task_core_id = tskNO_AFFINITY   // 不绑定核心（由调度器决定）
  };

  esp_err_t ret = esp_event_loop_create(&loop_args, &event_loop_);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
    return ret;
  }

  initialized_ = true;
  stats_ = {0};
  
  ESP_LOGI(TAG, "EventBus initialized successfully");
  ESP_LOGI(TAG, "  - Queue size: %d", loop_args.queue_size);
  ESP_LOGI(TAG, "  - Task priority: %d", loop_args.task_priority);
  ESP_LOGI(TAG, "  - Stack size: %d bytes", loop_args.task_stack_size);

  return ESP_OK;
}

void EventBus::Destroy() {
  if (!initialized_) {
    return;
  }

  if (event_loop_) {
    esp_event_loop_delete(event_loop_);
    event_loop_ = nullptr;
  }

  initialized_ = false;
  ESP_LOGI(TAG, "EventBus destroyed");
}

// ========== 订阅事件 ==========

// C++风格订阅（使用lambda）
struct HandlerWrapper {
  std::function<void(void*)> func;
};

static void cpp_handler_wrapper(void* arg, esp_event_base_t base, 
                                 int32_t id, void* event_data) {
  auto* wrapper = static_cast<HandlerWrapper*>(arg);
  if (wrapper && wrapper->func) {
    wrapper->func(event_data);
  }
}

esp_err_t EventBus::Subscribe(
    esp_event_base_t event_base,
    int32_t event_id,
    std::function<void(void*)> handler) {
  
  if (!initialized_) {
    ESP_LOGE(TAG, "EventBus not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // 创建wrapper（注意：需要手动管理内存，或者使用智能指针）
  auto* wrapper = new HandlerWrapper{handler};

  esp_err_t ret = esp_event_handler_instance_register_with(
      event_loop_,
      event_base,
      event_id,
      cpp_handler_wrapper,
      wrapper,
      nullptr
  );

  if (ret != ESP_OK) {
    delete wrapper;
    ESP_LOGE(TAG, "Failed to subscribe: %s", esp_err_to_name(ret));
  }

  return ret;
}

// ESP-IDF原生风格订阅
esp_err_t EventBus::Subscribe(
    esp_event_base_t event_base,
    int32_t event_id,
    esp_event_handler_t handler,
    void* handler_arg) {
  
  if (!initialized_) {
    ESP_LOGE(TAG, "EventBus not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = esp_event_handler_instance_register_with(
      event_loop_,
      event_base,
      event_id,
      handler,
      handler_arg,
      nullptr
  );

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe: %s", esp_err_to_name(ret));
  }

  return ret;
}

// ========== 发布事件 ==========

esp_err_t EventBus::Publish(
    esp_event_base_t event_base,
    int32_t event_id,
    const void* event_data,
    size_t event_data_size) {
  
  if (!initialized_) {
    ESP_LOGE(TAG, "EventBus not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = esp_event_post_to(
      event_loop_,
      event_base,
      event_id,
      event_data,
      event_data_size,
      portMAX_DELAY  // 阻塞等待
  );

  if (ret == ESP_OK) {
    stats_.total_published++;
  } else if (ret == ESP_ERR_TIMEOUT) {
    stats_.queue_overflow_count++;
    stats_.total_dropped++;
    ESP_LOGW(TAG, "Event queue full, event dropped");
  } else {
    ESP_LOGE(TAG, "Failed to publish event: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t EventBus::PublishNonBlocking(
    esp_event_base_t event_base,
    int32_t event_id,
    const void* event_data,
    size_t event_data_size) {
  
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = esp_event_post_to(
      event_loop_,
      event_base,
      event_id,
      event_data,
      event_data_size,
      0  // 非阻塞
  );

  if (ret == ESP_OK) {
    stats_.total_published++;
  } else if (ret == ESP_ERR_TIMEOUT) {
    stats_.queue_overflow_count++;
    stats_.total_dropped++;
  }

  return ret;
}

esp_err_t EventBus::PublishFromISR(
    esp_event_base_t event_base,
    int32_t event_id,
    const void* event_data,
    size_t event_data_size) {
  
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }

  BaseType_t task_woken = pdFALSE;
  esp_err_t ret = esp_event_isr_post_to(
      event_loop_,
      event_base,
      event_id,
      event_data,
      event_data_size,
      &task_woken
  );

  if (ret == ESP_OK) {
    stats_.total_published++;
    if (task_woken == pdTRUE) {
      portYIELD_FROM_ISR();
    }
  } else {
    stats_.total_dropped++;
  }

  return ret;
}

void EventBus::ResetStats() {
  stats_ = {0};
}

}  // namespace xiaozhi























