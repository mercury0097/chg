#include "board.h"
#include "config.h"
#include "device_state_event.h"
#include "dog_movements.h"
#include "mcp_server.h"
#include "settings.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string>

#define TAG "DogController"

// Dog动作参数
struct DogActionParams {
  int action_type;
  float steps;
  int speed;
  int direction;
  int amount;
};

// 动作类型枚举
enum ActionType {
  ACTION_WALK_FORWARD = 1,
  ACTION_WALK_BACKWARD = 2,
  ACTION_HOME = 3,
  ACTION_TURN_RIGHT = 4,
  ACTION_TURN_LEFT = 5,
  ACTION_SAY_HELLO = 6,
  ACTION_SWAY_BACK_FORTH = 7,
  ACTION_PUSH_UP = 8,
  ACTION_SLEEP = 9
};

class DogController {
public:
  // 需要被API适配器访问的成员
  Dog dog_;
  bool is_action_in_progress_ = false;
  bool is_sleeping_ = false;
  bool sleep_abort_ = false;

  void QueueAction(ActionType action_type, float steps = 1, int speed = 1000,
                   int direction = 0, int amount = 30) {
    DogActionParams params = {
        .action_type = (int)action_type,
        .steps = steps,
        .speed = speed,
        .direction = direction,
        .amount = amount};
    xQueueSend(action_queue_, &params, 0);
  }

private:
  TaskHandle_t action_task_handle_ = nullptr;
  TaskHandle_t idle_task_handle_ = nullptr;
  QueueHandle_t action_queue_;
  bool sleep_pending_ = false;

  static void ActionTask(void *arg) {
    DogController *controller = static_cast<DogController *>(arg);
    DogActionParams params;
    controller->dog_.AttachServos();

    while (true) {
      if (xQueueReceive(controller->action_queue_, &params,
                        pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "执行动作: %d", params.action_type);
        controller->is_action_in_progress_ = true;
        if (params.action_type != ACTION_SLEEP) {
          controller->is_sleeping_ = false;
        }

        switch (params.action_type) {
        case ACTION_WALK_FORWARD:
          controller->dog_.WalkForward(params.steps, params.speed, params.amount);
          break;
        case ACTION_WALK_BACKWARD:
          controller->dog_.WalkBackward(params.steps, params.speed, params.amount);
          break;
        case ACTION_TURN_RIGHT:
          controller->dog_.TurnRight(params.steps, params.speed, params.amount);
          break;
        case ACTION_TURN_LEFT:
          controller->dog_.TurnLeft(params.steps, params.speed, params.amount);
          break;
        case ACTION_SAY_HELLO:
          controller->dog_.SayHello((int)params.steps, params.speed, params.amount);
          break;
        case ACTION_SWAY_BACK_FORTH:
          controller->dog_.SwayBackForth((int)params.steps, params.speed, params.amount);
          break;
        case ACTION_PUSH_UP:
          controller->dog_.PushUp((int)params.steps, params.speed, params.amount);
          break;
        case ACTION_SLEEP:
          controller->dog_.SleepPose();
          controller->is_sleeping_ = true;
          controller->sleep_abort_ = false;
          for (int elapsed_ms = 0; elapsed_ms < 4000; elapsed_ms += 100) {
            if (controller->sleep_abort_) {
              break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          controller->dog_.ForceHome();
          controller->is_sleeping_ = false;
          break;
        case ACTION_HOME:
          controller->dog_.Home();
          break;
        default:
          ESP_LOGW(TAG, "未知的动作类型: %d", params.action_type);
          break;
        }
        
        // 每个动作完成后自动回到中立位置(除非动作本身就是Home)
        if (params.action_type != ACTION_HOME &&
            params.action_type != ACTION_SLEEP) {
          ESP_LOGI(TAG, "动作完成，回到中立位置");
          controller->dog_.Home();
        }

        controller->is_action_in_progress_ = false;
        vTaskDelay(pdMS_TO_TICKS(20));
      }
    }
  }

  // 空闲时持续复位任务 - 参考palqiqi的IdleActionTask
  static void IdleResetTask(void *arg) {
    DogController *controller = static_cast<DogController *>(arg);
    
    // 等待系统启动完成
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(2000)); // 每2秒检查一次
      
      // 如果没有动作正在执行，就复位
      if (!controller->is_action_in_progress_) {
        if (controller->sleep_pending_) {
          ESP_LOGI(TAG, "进入睡觉姿态");
          controller->dog_.SleepPose();
          controller->is_sleeping_ = true;
          controller->sleep_pending_ = false;
        }
        ESP_LOGI(TAG, "空闲复位");
        controller->dog_.Home();
      }
    }
  }

  void LoadTrims() {
    Settings settings("dog_trims", false);
    int left_front_leg = settings.GetInt("left_front_leg", 0);
    int right_front_leg = settings.GetInt("right_front_leg", 0);
    int left_rear_leg = settings.GetInt("left_rear_leg", 0);
    int right_rear_leg = settings.GetInt("right_rear_leg", 0);
    dog_.SetTrims(left_rear_leg, left_front_leg, right_front_leg, right_rear_leg);
    ESP_LOGI(TAG, "加载舵机微调值: LF=%d, RF=%d, LR=%d, RR=%d",
             left_front_leg, right_front_leg, left_rear_leg, right_rear_leg);
  }

  void OnDeviceStateChange(DeviceState previous_state, DeviceState current_state) {
    if (previous_state == kDeviceStateListening &&
        current_state == kDeviceStateIdle) {
      sleep_pending_ = true;
    }

    if (current_state != kDeviceStateIdle) {
      sleep_pending_ = false;
      sleep_abort_ = true;
      if (is_sleeping_ && !is_action_in_progress_) {
        ESP_LOGI(TAG, "唤醒到中立位置");
        dog_.ForceHome();
        is_sleeping_ = false;
      } else if (!is_action_in_progress_) {
        dog_.ForceHome();
      }
    }
  }

public:
  DogController() {
    // Init参数顺序: left_rear_leg, left_front_leg, right_front_leg, right_rear_leg
    dog_.Init(LEFT_REAR_LEG_PIN, LEFT_FRONT_LEG_PIN, RIGHT_FRONT_LEG_PIN,
              RIGHT_REAR_LEG_PIN);
    ESP_LOGI(TAG, "Dog机器人初始化");
    action_queue_ = xQueueCreate(10, sizeof(DogActionParams));
    xTaskCreate(ActionTask, "dog_action", 1024 * 3, this, 5, &action_task_handle_);
    
    // 启动空闲复位任务
    xTaskCreate(IdleResetTask, "dog_idle_reset", 1024 * 2, this, 4, &idle_task_handle_);

    LoadTrims();
    
    // 开机时立即复位到中立位置
    dog_.Home();

    DeviceStateEventManager::GetInstance().RegisterStateChangeCallback(
        [this](DeviceState previous_state, DeviceState current_state) {
          OnDeviceStateChange(previous_state, current_state);
        });
    
    auto &mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.dog.walk_forward", "小狗向前走",
        PropertyList({
            Property("steps", kPropertyTypeInteger, 8),
            Property("speed", kPropertyTypeInteger, 1000),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_WALK_FORWARD, steps, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool(
        "self.dog.walk_backward", "小狗向后退",
        PropertyList({
            Property("steps", kPropertyTypeInteger, 8),
            Property("speed", kPropertyTypeInteger, 1000),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_WALK_BACKWARD, steps, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool(
        "self.dog.turn_right", "小狗向右转",
        PropertyList({
            Property("steps", kPropertyTypeInteger, 8),
            Property("speed", kPropertyTypeInteger, 1000),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_TURN_RIGHT, steps, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool(
        "self.dog.turn_left", "小狗向左转",
        PropertyList({
            Property("steps", kPropertyTypeInteger, 8),
            Property("speed", kPropertyTypeInteger, 1000),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_TURN_LEFT, steps, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool(
        "self.dog.say_hello", "小狗打招呼（招手）",
        PropertyList({
            Property("wave_times", kPropertyTypeInteger, 5),
            Property("speed", kPropertyTypeInteger, 500),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int wave_times = properties["wave_times"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_SAY_HELLO, wave_times, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool(
        "self.dog.sway_back_forth", "前后摇摆",
        PropertyList({
            Property("steps", kPropertyTypeInteger, 5),
            Property("speed", kPropertyTypeInteger, 1000),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_SWAY_BACK_FORTH, steps, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool(
        "self.dog.push_up", "俯卧撑",
        PropertyList({
            Property("steps", kPropertyTypeInteger, 4),
            Property("speed", kPropertyTypeInteger, 1000),
            Property("amount", kPropertyTypeInteger, 30),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int steps = properties["steps"].value<int>();
          int speed = properties["speed"].value<int>();
          int amount = properties["amount"].value<int>();
          QueueAction(ACTION_PUSH_UP, steps, speed, 0, amount);
          return "OK";
        });

    mcp_server.AddTool("self.dog.sleep", "睡觉", PropertyList(),
                        [this](const PropertyList &properties) -> ReturnValue {
                          QueueAction(ACTION_SLEEP, 1, 0, 0, 0);
                          return "OK";
                        });

    mcp_server.AddTool("self.dog.stop", "立即停止", PropertyList(),
                        [this](const PropertyList &properties) -> ReturnValue {
                          sleep_abort_ = true;
                          is_sleeping_ = false;
                          dog_.ForceHome();
                          return "OK";
                        });

    mcp_server.AddTool("self.dog.home", "回到休息姿态", PropertyList(),
                        [this](const PropertyList &properties) -> ReturnValue {
                          sleep_abort_ = true;
                          is_sleeping_ = false;
                          dog_.ForceHome();
                          return "OK";
                        });

    mcp_server.AddTool(
        "self.dog.set_trim",
        "校准单个舵机位置。设置指定舵机的微调参数以调整Dog的初始站立姿态，设置"
        "后会保存到NVS。",
        PropertyList({
            Property("left_front_leg", kPropertyTypeInteger, 0),
            Property("right_front_leg", kPropertyTypeInteger, 0),
            Property("left_rear_leg", kPropertyTypeInteger, 0),
            Property("right_rear_leg", kPropertyTypeInteger, 0),
        }),
        [this](const PropertyList &properties) -> ReturnValue {
          int left_front_leg = properties["left_front_leg"].value<int>();
          int right_front_leg = properties["right_front_leg"].value<int>();
          int left_rear_leg = properties["left_rear_leg"].value<int>();
          int right_rear_leg = properties["right_rear_leg"].value<int>();

          Settings settings("dog_trims", true);
          settings.SetInt("left_front_leg", left_front_leg);
          settings.SetInt("right_front_leg", right_front_leg);
          settings.SetInt("left_rear_leg", left_rear_leg);
          settings.SetInt("right_rear_leg", right_rear_leg);

          dog_.SetTrims(left_rear_leg, left_front_leg, right_front_leg,
                        right_rear_leg);
          dog_.Home();
          return "OK";
        });

    mcp_server.AddTool("self.dog.get_trims", "获取当前的舵机微调设置", PropertyList(),
                        [](const PropertyList &properties) -> ReturnValue {
                          Settings settings("dog_trims", false);
                          int left_front_leg = settings.GetInt("left_front_leg", 0);
                          int right_front_leg = settings.GetInt("right_front_leg", 0);
                          int left_rear_leg = settings.GetInt("left_rear_leg", 0);
                          int right_rear_leg = settings.GetInt("right_rear_leg", 0);

                          std::string trims =
                              "{\"left_front_leg\":" +
                              std::to_string(left_front_leg) +
                              ",\"right_front_leg\":" +
                              std::to_string(right_front_leg) +
                              ",\"left_rear_leg\":" +
                              std::to_string(left_rear_leg) +
                              ",\"right_rear_leg\":" +
                              std::to_string(right_rear_leg) + "}";
                          return trims;
                        });

    mcp_server.AddTool("self.dog.get_status", "获取机器人当前状态", PropertyList(),
                        [this](const PropertyList &properties) -> ReturnValue {
                          return is_action_in_progress_ ? "moving" : "idle";
                        });

    mcp_server.AddTool("self.battery.get_level", "获取机器人电池电量和充电状态",
                        PropertyList(),
                        [](const PropertyList &properties) -> ReturnValue {
                          auto &board = Board::GetInstance();
                          int level = 0;
                          bool charging = false;
                          bool discharging = false;
                          board.GetBatteryLevel(level, charging, discharging);

                          std::string status =
                              "{\"level\":" + std::to_string(level) +
                              ",\"charging\":" + (charging ? "true" : "false") +
                              "}";
                          return status;
                        });

    ESP_LOGI(TAG, "MCP工具注册完成");
  }

  ~DogController() {
    if (action_task_handle_ != nullptr) {
      vTaskDelete(action_task_handle_);
    }
    if (idle_task_handle_ != nullptr) {
      vTaskDelete(idle_task_handle_);
    }
    if (action_queue_ != nullptr) {
      vQueueDelete(action_queue_);
    }
  }
};

static DogController *g_dog_controller = nullptr;

void InitializeDogController() {
  if (g_dog_controller == nullptr) {
    g_dog_controller = new DogController();
  }
}

void DogWalkForward(int steps, int speed, int amount) {
  ESP_LOGI(TAG, "DogWalkForward called: steps=%d, speed=%d, amount=%d", steps, speed, amount);
}

void DogWalkBackward(int steps, int speed, int amount) {
  ESP_LOGI(TAG, "DogWalkBackward called: steps=%d, speed=%d, amount=%d", steps, speed, amount);
}

// Otto 兼容函数 - application.cc中会调用
void OttoSwing(int steps, int speed, int amount) {
  ESP_LOGI(TAG, "OttoSwing called on Dog board (no-op): steps=%d, speed=%d, amount=%d", 
           steps, speed, amount);
  // Dog机器人不支持Swing动作，这是一个空实现以保持兼容性
  // 如果需要,可以实现一个类似的dog动作
}

void OttoJump(int steps, int speed) {
  ESP_LOGI(TAG, "OttoJump called on Dog board (no-op): steps=%d, speed=%d", 
           steps, speed);
  // Dog机器人不支持Jump动作，这是一个空实现以保持兼容性
  // 如果需要,可以实现一个类似的dog动作
}

// ==================== HTTP API适配器导出函数 ====================

// 当前正在执行的动作名称
static std::string g_current_action = "";

bool DogControllerIsIdle() {
  if (g_dog_controller == nullptr) {
    return true;
  }
  return !g_dog_controller->is_action_in_progress_;
}

std::string DogControllerGetCurrentAction() {
  if (g_dog_controller == nullptr || !g_dog_controller->is_action_in_progress_) {
    return "";
  }
  return g_current_action;
}

bool DogControllerExecuteAction(const std::string& action, int steps, int speed) {
  if (g_dog_controller == nullptr) {
    ESP_LOGE(TAG, "控制器未初始化");
    return false;
  }

  // 记录当前动作名称
  g_current_action = action;

  // 动作名到ActionType的映射
  if (action == "walk_forward") {
    g_dog_controller->QueueAction(ACTION_WALK_FORWARD, steps, speed, 0, 30);
  } else if (action == "walk_backward") {
    g_dog_controller->QueueAction(ACTION_WALK_BACKWARD, steps, speed, 0, 30);
  } else if (action == "turn_left") {
    g_dog_controller->QueueAction(ACTION_TURN_LEFT, steps, speed, 0, 30);
  } else if (action == "turn_right") {
    g_dog_controller->QueueAction(ACTION_TURN_RIGHT, steps, speed, 0, 30);
  } else if (action == "home") {
    g_dog_controller->QueueAction(ACTION_HOME, 1, 0, 0, 0);
  } else if (action == "stop") {
    g_dog_controller->sleep_abort_ = true;
    g_dog_controller->is_sleeping_ = false;
    g_dog_controller->dog_.ForceHome();
    g_current_action = "";
  } else if (action == "say_hello") {
    g_dog_controller->QueueAction(ACTION_SAY_HELLO, steps, speed, 0, 30);
  } else if (action == "sway_back_forth") {
    g_dog_controller->QueueAction(ACTION_SWAY_BACK_FORTH, steps, speed, 0, 30);
  } else if (action == "push_up") {
    g_dog_controller->QueueAction(ACTION_PUSH_UP, steps, speed, 0, 30);
  } else if (action == "sleep") {
    g_dog_controller->QueueAction(ACTION_SLEEP, 1, 0, 0, 0);
  } else {
    ESP_LOGW(TAG, "未知的动作: %s", action.c_str());
    g_current_action = "";
    return false;
  }

  ESP_LOGI(TAG, "API执行动作: %s, steps=%d, speed=%d", action.c_str(), steps, speed);
  return true;
}
