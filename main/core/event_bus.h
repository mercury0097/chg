#pragma once

#include <esp_event.h>
#include <esp_err.h>
#include <functional>
#include <string>

namespace xiaozhi {

// ============================================================================
// 事件域定义（Event Bases）
// ============================================================================

// 宠物系统事件
ESP_EVENT_DECLARE_BASE(PET_EVENT);

// 表情/UI事件
ESP_EVENT_DECLARE_BASE(EMO_EVENT);

// 逻辑事件（小智核心逻辑）
ESP_EVENT_DECLARE_BASE(LOGIC_EVENT);

// 云端/网络事件
ESP_EVENT_DECLARE_BASE(CLOUD_EVENT);

// 学习系统事件
ESP_EVENT_DECLARE_BASE(LEARNING_EVENT);

// ============================================================================
// 事件ID定义
// ============================================================================

// 宠物事件ID
enum PetEventId {
  PET_STATE_CHANGED = 1,      // 宠物状态改变
  PET_DAILY_TASK_DONE,        // 完成每日任务
  PET_MOOD_CRITICAL,          // 心情极差（<20）
  PET_HUNGRY,                 // 饥饿（饱腹度<30）
  PET_DIRTY,                  // 脏了（清洁度<30）
};

// 表情事件ID
enum EmoEventId {
  EMO_SET = 1,                // 设置表情
  EMO_BLINK,                  // 眨眼
  EMO_TTS_SPEAKING,           // TTS播放中
  EMO_TTS_STOPPED,            // TTS停止
};

// 逻辑事件ID
enum LogicEventId {
  LOGIC_VAD_VOICE_START = 1,  // VAD检测到人声开始
  LOGIC_VAD_VOICE_END,        // VAD检测到人声结束
  LOGIC_WAKE_WORD_DETECTED,   // 唤醒词检测
  LOGIC_TTS_START,            // TTS开始播放
  LOGIC_TTS_END,              // TTS结束播放
  LOGIC_CONVERSATION_START,   // 对话开始
  LOGIC_CONVERSATION_END,     // 对话结束
  LOGIC_INTENT_PARSED,        // 意图解析完成
  LOGIC_USER_FEEDBACK,        // 用户反馈（点赞/点踩）
};

// 云端事件ID
enum CloudEventId {
  CLOUD_CONNECTED = 1,        // 云端连接成功
  CLOUD_DISCONNECTED,         // 云端断开连接
  CLOUD_CMD_RECEIVED,         // 收到云端指令
  CLOUD_REPLY_SENT,           // 发送回复成功
  CLOUD_ERROR,                // 云端通讯错误
};

// 学习事件ID
enum LearningEventId {
  LEARNING_PROFILE_UPDATED = 1, // 用户画像更新
  LEARNING_DECISION_MADE,       // 做出决策
};

// ============================================================================
// 事件数据结构
// ============================================================================

// 宠物状态事件数据
struct PetStateEventData {
  int mood;
  int satiety;
  int cleanliness;
  int overall;
};

// 表情事件数据
struct EmoEventData {
  char emotion[32];  // 表情名称
};

// 对话事件数据
struct ConversationEventData {
  char topic[64];         // 话题类型（如 "weather", "pet", "story"）
  bool positive_feedback; // 是否正面反馈
  uint32_t duration_ms;   // 对话时长（毫秒）
};

// 用户反馈事件数据
struct UserFeedbackData {
  bool is_positive;  // true=点赞，false=点踩
  char context[32];  // 反馈上下文
};

// ============================================================================
// 事件总线类
// ============================================================================

class EventBus {
 public:
  static EventBus& GetInstance();

  // 初始化事件总线
  esp_err_t Initialize();

  // 销毁事件总线
  void Destroy();

  // ========== 订阅事件 ==========
  
  // 订阅事件（C++风格，使用lambda）
  esp_err_t Subscribe(
      esp_event_base_t event_base,
      int32_t event_id,
      std::function<void(void*)> handler
  );

  // 订阅事件（ESP-IDF原生风格）
  esp_err_t Subscribe(
      esp_event_base_t event_base,
      int32_t event_id,
      esp_event_handler_t handler,
      void* handler_arg = nullptr
  );

  // ========== 发布事件 ==========
  
  // 发布事件（阻塞，等待所有handler执行完）
  esp_err_t Publish(
      esp_event_base_t event_base,
      int32_t event_id,
      const void* event_data = nullptr,
      size_t event_data_size = 0
  );

  // 发布事件（非阻塞，立即返回）
  esp_err_t PublishNonBlocking(
      esp_event_base_t event_base,
      int32_t event_id,
      const void* event_data = nullptr,
      size_t event_data_size = 0
  );

  // 从ISR发布事件
  esp_err_t PublishFromISR(
      esp_event_base_t event_base,
      int32_t event_id,
      const void* event_data = nullptr,
      size_t event_data_size = 0
  );

  // ========== 辅助工具 ==========
  
  // 获取事件循环句柄（供高级用户直接使用）
  esp_event_loop_handle_t GetLoopHandle() const { return event_loop_; }

  // 获取事件统计
  struct EventStats {
    uint32_t total_published;
    uint32_t total_dropped;
    uint32_t queue_overflow_count;
  };
  EventStats GetStats() const { return stats_; }

  // 重置统计
  void ResetStats();

 private:
  EventBus() = default;
  ~EventBus() = default;
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  esp_event_loop_handle_t event_loop_ = nullptr;
  bool initialized_ = false;
  EventStats stats_ = {0};
};

// ============================================================================
// 便捷宏定义
// ============================================================================

// 订阅事件宏（简化调用）
#define SUBSCRIBE_EVENT(base, id, handler) \
  EventBus::GetInstance().Subscribe(base, id, handler)

// 发布事件宏
#define PUBLISH_EVENT(base, id, data_ptr, data_size) \
  EventBus::GetInstance().Publish(base, id, data_ptr, data_size)

// 发布事件（无数据）
#define PUBLISH_EVENT_SIMPLE(base, id) \
  EventBus::GetInstance().Publish(base, id, nullptr, 0)

}  // namespace xiaozhi























