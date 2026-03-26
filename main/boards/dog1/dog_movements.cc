#include "dog_movements.h"
#include <math.h>
#include <string.h>

static const char *TAG = "DogMovements";

namespace {

constexpr int kDogNeutralAngle = 90;

// Real-world servo direction after removing the historical pin remap:
// - Left legs: smaller angle = forward, larger angle = backward
// - Right legs: larger angle = forward, smaller angle = backward
inline int LeftForward(int neutral, int amount) { return neutral - amount; }
inline int LeftBackward(int neutral, int amount) { return neutral + amount; }
inline int RightForward(int neutral, int amount) { return neutral + amount; }
inline int RightBackward(int neutral, int amount) { return neutral - amount; }
inline int CurrentLogicalPosition(Oscillator &servo, int trim) {
  return servo.GetPosition() - trim;
}

}  // namespace

// millis() 函数实现
unsigned long IRAM_ATTR millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

// 缓动函数实现
static float ApplyEasing(float t, EaseType ease_type) {
  switch (ease_type) {
  case EASE_LINEAR:
    return t;
  case EASE_IN_OUT: // S 型曲线
    return t * t * (3.0f - 2.0f * t);
  case EASE_IN: // 慢启动
    return t * t;
  case EASE_OUT: // 慢结束
    return t * (2.0f - t);
  case EASE_IN_BACK: // 回弹启动
    return t * t * (2.70158f * t - 1.70158f);
  case EASE_OUT_BACK: { // 回弹结束
    float s = 1.70158f;
    t -= 1.0f;
    return t * t * ((s + 1.0f) * t + s) + 1.0f;
  }
  case EASE_OUT_BOUNCE: { // 弹跳结束
    if (t < (1.0f / 2.75f)) {
      return 7.5625f * t * t;
    } else if (t < (2.0f / 2.75f)) {
      t -= 1.5f / 2.75f;
      return 7.5625f * t * t + 0.75f;
    } else if (t < (2.5f / 2.75f)) {
      t -= 2.25f / 2.75f;
      return 7.5625f * t * t + 0.9375f;
    } else {
      t -= 2.625f / 2.75f;
      return 7.5625f * t * t + 0.984375f;
    }
  }
  default:
    return t;
  }
}

//--------------------------------------------------------------
//-- Dog构造函数
//--------------------------------------------------------------
Dog::Dog() {
  is_dog_resting_ = false;
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_pins_[i] = 0;
    servo_trim_[i] = 0;
    increment_[i] = 0;
  }
  final_time_ = 0;
  partial_time_ = 0;
}

//--------------------------------------------------------------
//-- Dog析构函数
//--------------------------------------------------------------
Dog::~Dog() { DetachServos(); }

//--------------------------------------------------------------
//-- Dog初始化
//--------------------------------------------------------------
void Dog::Init(int left_rear_leg, int left_front_leg, int right_front_leg,
               int right_rear_leg) {
  servo_pins_[LEFT_REAR_LEG] = left_rear_leg;
  servo_pins_[LEFT_FRONT_LEG] = left_front_leg;
  servo_pins_[RIGHT_FRONT_LEG] = right_front_leg;
  servo_pins_[RIGHT_REAR_LEG] = right_rear_leg;

  AttachServos();
  is_dog_resting_ = false;
}

//--------------------------------------------------------------
//-- 连接舵机
//--------------------------------------------------------------
void Dog::AttachServos() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].Attach(servo_pins_[i]);
  }
}

//--------------------------------------------------------------
//-- 断开舵机
//--------------------------------------------------------------
void Dog::DetachServos() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].Detach();
  }
}

//--------------------------------------------------------------
//-- 设置舵机微调
//--------------------------------------------------------------
void Dog::SetTrims(int left_rear_leg, int left_front_leg, int right_front_leg,
                   int right_rear_leg) {
  servo_trim_[LEFT_REAR_LEG] = left_rear_leg;
  servo_trim_[LEFT_FRONT_LEG] = left_front_leg;
  servo_trim_[RIGHT_FRONT_LEG] = right_front_leg;
  servo_trim_[RIGHT_REAR_LEG] = right_rear_leg;
}

//--------------------------------------------------------------
//-- 移动所有舵机到指定位置
//--------------------------------------------------------------
void Dog::MoveServos(int time, int servo_target[]) {
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  if (time > 10) {
    int start_position[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
      start_position[i] = CurrentLogicalPosition(servo_[i], servo_trim_[i]);
      increment_[i] =
          ((servo_target[i]) - start_position[i]) / (time / 10.0f);
    }

    final_time_ = esp_timer_get_time() / 1000 + time;

    for (int iteration = 1; esp_timer_get_time() / 1000 < final_time_; iteration++) {
      partial_time_ = esp_timer_get_time() / 1000 + 10;
      for (int i = 0; i < SERVO_COUNT; i++) {
        int logical_position =
            (int)roundf(start_position[i] + increment_[i] * iteration);
        servo_[i].SetPosition(logical_position + servo_trim_[i]);
      }
      while (esp_timer_get_time() / 1000 < partial_time_)
        ; // 等待
    }
  } else {
    for (int i = 0; i < SERVO_COUNT; i++) {
      servo_[i].SetPosition(servo_target[i] + servo_trim_[i]);
    }
  }
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].SetPosition(servo_target[i] + servo_trim_[i]);
  }
}

//--------------------------------------------------------------
//-- 移动单个舵机
//--------------------------------------------------------------
void Dog::MoveSingle(int position, int servo_number) {
  if (GetRestState() == true) {
    SetRestState(false);
  }
  int servo_target[SERVO_COUNT];
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_target[i] = CurrentLogicalPosition(servo_[i], servo_trim_[i]);
  }
  servo_target[servo_number] = position;
  MoveServos(200, servo_target);
}

//--------------------------------------------------------------
//-- 振荡运动
//--------------------------------------------------------------
void Dog::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                          int period, double phase_diff[SERVO_COUNT], float cycle) {
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].SetO(offset[i]);
    servo_[i].SetA(amplitude[i]);
    servo_[i].SetT(period);
    servo_[i].SetPh(phase_diff[i]);
  }
  unsigned long ref = esp_timer_get_time() / 1000;
  unsigned long end_time = ref + (unsigned long)(period * cycle);
  
  while (esp_timer_get_time() / 1000 <= end_time) {
    for (int i = 0; i < SERVO_COUNT; i++) {
      servo_[i].Refresh();
    }
    // 添加延迟以避免看门狗超时，10ms对舵机控制来说足够快
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//--------------------------------------------------------------
//-- 执行复杂运动
//--------------------------------------------------------------
void Dog::Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                  double phase_diff[SERVO_COUNT], float steps) {
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }
  OscillateServos(amplitude, offset, period, phase_diff, steps);
}

//--------------------------------------------------------------
//-- HOME = Dog休息姿态
//--------------------------------------------------------------
void Dog::Home() {
  if (is_dog_resting_ == false) {
    int servo_position[SERVO_COUNT] = {90, 90, 90, 90};
    MoveServos(500, servo_position);
    // Keep servos attached so neutral pose is actively held.
    // Detach in SleepPose when we intentionally want limp/power-save behavior.
    is_dog_resting_ = true;
  }
}

void Dog::ForceHome() {
  int servo_position[SERVO_COUNT] = {90, 90, 90, 90};
  MoveServos(500, servo_position);
  // ForceHome is used as an immediate safety stop; keep holding neutral.
  is_dog_resting_ = true;
}

void Dog::SleepPose() {
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  // Real-world "sleep" posture: all four legs sprawl toward the head side.
  // Real indexes: [0]=LF [1]=LR [2]=RF [3]=RR
  int servo_position[SERVO_COUNT] = {
      50,  // LF forward
      50,  // LR forward
      130, // RF forward
      130  // RR forward
  };
  MoveServosWithEase(600, servo_position, EASE_IN_OUT);
  DetachServos();
  is_dog_resting_ = true;
}

bool Dog::GetRestState() { return is_dog_resting_; }

void Dog::SetRestState(bool state) { is_dog_resting_ = state; }

//--------------------------------------------------------------
//-- Dog前进动作 - 八步对角线步态
//-- 步态说明：
//-- 对角线组A: 左后腿 + 右前腿
//-- 对角线组B: 左前腿 + 右后腿
//--
//-- 真实物理方向：
//-- 左侧：角度减小 = 向前摆
//-- 右侧：角度增大 = 向前摆
//--------------------------------------------------------------
void Dog::WalkForward(float steps, int period, int amount) {
  ESP_LOGI(TAG, "前进(八步对角线步态): steps=%.1f, period=%d, amount=%d", steps, period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  int step_time = period / 8;
  int neutral = kDogNeutralAngle;
  int left_forward = LeftForward(neutral, amount);
  int left_backward = LeftBackward(neutral, amount);
  int right_forward = RightForward(neutral, amount);
  int right_backward = RightBackward(neutral, amount);

  for (int step = 0; step < (int)steps; step++) {
    {
      int target[SERVO_COUNT] = {
          neutral,       // 左前支撑
          left_forward,  // 左后向前摆
          right_forward, // 右前向前摆
          neutral        // 右后支撑
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_backward, // 左前向后蹬
          left_forward,  // 左后保持前方
          right_forward, // 右前保持前方
          right_backward // 右后向后蹬
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_backward, // 左前保持后方
          neutral,       // 左后还原
          neutral,       // 右前还原
          right_backward // 右后保持后方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward, // 左前向前摆
          neutral,      // 左后支撑
          neutral,      // 右前支撑
          right_forward // 右后向前摆
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward,  // 左前保持前方
          left_backward, // 左后向后蹬
          right_backward, // 右前向后蹬
          right_forward  // 右后保持前方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          neutral,       // 左前还原
          left_backward, // 左后保持后方
          right_backward, // 右前保持后方
          neutral        // 右后还原
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 注意: Home()由controller统一调用,这里不调用
}


void Dog::WalkBackward(float steps, int period, int amount) {
  ESP_LOGI(TAG, "后退(八步对角线步态-逆序): steps=%.1f, period=%d, amount=%d", steps, period, amount);
  
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }
  
  int step_time = period / 8;
  int neutral = kDogNeutralAngle;
  int left_forward = LeftForward(neutral, amount);
  int left_backward = LeftBackward(neutral, amount);
  int right_forward = RightForward(neutral, amount);
  int right_backward = RightBackward(neutral, amount);
  
  for (int step = 0; step < (int)steps; step++) {
    {
      int target[SERVO_COUNT] = {
          neutral,        // 左前支撑
          left_backward,  // 左后向后摆
          right_backward, // 右前向后摆
          neutral         // 右后支撑
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward,  // 左前向前蹬
          left_backward, // 左后保持后方
          right_backward, // 右前保持后方
          right_forward  // 右后向前蹬
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward,  // 左前保持前方
          neutral,       // 左后还原
          neutral,       // 右前还原
          right_forward  // 右后保持前方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_backward, // 左前向后摆
          neutral,       // 左后支撑
          neutral,       // 右前支撑
          right_backward // 右后向后摆
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_backward, // 左前保持后方
          left_forward,  // 左后向前蹬
          right_forward, // 右前向前蹬
          right_backward // 右后保持后方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          neutral,      // 左前还原
          left_forward, // 左后保持前方
          right_forward, // 右前保持前方
          neutral       // 右后还原
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog前后摇摆动作 - 四腿同步前后摆动
//--------------------------------------------------------------
void Dog::SwayBackForth(int steps, int period, int amount) {
  ESP_LOGI(TAG, "前后摇摆: steps=%d, period=%d, amount=%d", steps, period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  int half_time = period / 2;
  int neutral = kDogNeutralAngle;
  int left_forward = LeftForward(neutral, amount);
  int left_backward = LeftBackward(neutral, amount);
  int right_forward = RightForward(neutral, amount);
  int right_backward = RightBackward(neutral, amount);

  for (int i = 0; i < steps; i++) {
    int target_back[SERVO_COUNT] = {
      left_backward,  // 左前腿
      left_backward,  // 左后腿
      right_backward, // 右前腿
      right_backward  // 右后腿
    };
    MoveServosWithEase(half_time, target_back, EASE_IN_OUT);

    int target_forward[SERVO_COUNT] = {
      left_forward,  // 左前腿
      left_forward,  // 左后腿
      right_forward, // 右前腿
      right_forward  // 右后腿
    };
    MoveServosWithEase(half_time, target_forward, EASE_IN_OUT);

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog俯卧撑动作 - 前腿前后摆动，后腿保持中立
//--------------------------------------------------------------
void Dog::PushUp(int steps, int period, int amount) {
  ESP_LOGI(TAG, "俯卧撑: steps=%d, period=%d, amount=%d", steps, period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  int half_time = period / 2;
  int neutral = kDogNeutralAngle;

  int push_amount = amount * 2;

  int left_forward = LeftForward(neutral, push_amount);
  int right_forward = RightForward(neutral, push_amount);

  for (int i = 0; i < steps; i++) {
    int target_forward[SERVO_COUNT] = {
      left_forward,  // 左前腿向前
      neutral,       // 左后腿保持中立
      right_forward, // 右前腿向前
      neutral        // 右后腿保持中立
    };
    MoveServosWithEase(half_time, target_forward, EASE_IN_OUT);

    int target_neutral[SERVO_COUNT] = {
      neutral, // 左后腿中立
      neutral, // 左前腿中立
      neutral, // 右前腿中立
      neutral  // 右后腿中立
    };
    MoveServosWithEase(half_time, target_neutral, EASE_IN_OUT);

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog右转动作 - 原左转的逆序（4-3-2-1）
//--------------------------------------------------------------
void Dog::TurnRight(float steps, int period, int amount) {
  ESP_LOGI(TAG, "右转(四步对角线步态): steps=%.1f, period=%d, amount=%d", steps, period, amount);
  
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }
  
  int step_time = period / 4;
  int neutral = kDogNeutralAngle;
  int left_forward = LeftForward(neutral, amount);
  int left_backward = LeftBackward(neutral, amount);
  int right_forward = RightForward(neutral, amount);
  int right_backward = RightBackward(neutral, amount);
  
  for (int step = 0; step < (int)steps; step++) {
    {
      int target[SERVO_COUNT] = {
          left_forward, // 左前向前
          neutral,      // 左后支撑
          neutral,      // 右前支撑
          right_backward // 右后向后
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward,  // 左前保持前方
          left_backward, // 左后向后
          right_forward, // 右前向前
          right_backward // 右后保持后方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          neutral,      // 左前还原
          left_backward, // 左后保持后方
          right_forward, // 右前保持前方
          neutral        // 右后还原
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog左转动作 - 四步对角线步态
//-- 步态说明：
//-- 对角线组A: 左后腿 + 右前腿
//-- 对角线组B: 左前腿 + 右后腿
//--
//-- 左转产生逆时针力矩：
//-- 第1步: 左后往后 + 右前往前（组A产生力矩）
//-- 第2步: 左前往前 + 右后往后（组B产生力矩）
//-- 第3步: 左前和右后保持
//-- 第4步: 全部回中立
//--------------------------------------------------------------
void Dog::TurnLeft(float steps, int period, int amount) {
  ESP_LOGI(TAG, "左转(四步对角线步态): steps=%.1f, period=%d, amount=%d", steps, period, amount);
  
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }
  
  int step_time = period / 4;
  int neutral = kDogNeutralAngle;
  int left_forward = LeftForward(neutral, amount);
  int left_backward = LeftBackward(neutral, amount);
  int right_forward = RightForward(neutral, amount);
  int right_backward = RightBackward(neutral, amount);
  
  for (int step = 0; step < (int)steps; step++) {
    {
      int target[SERVO_COUNT] = {
          neutral,       // 左前支撑
          left_backward, // 左后向后
          right_forward, // 右前向前
          neutral        // 右后支撑
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward,  // 左前向前
          left_backward, // 左后保持后方
          right_forward, // 右前保持前方
          right_backward // 右后向后
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {
          left_forward,  // 左前保持前方
          neutral,       // 左后还原
          neutral,       // 右前还原
          right_backward // 右后保持后方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    {
      int target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog打招呼动作 - 模仿小狗招手
//-- 步态说明：
//-- 1. 后两腿同时向前（模拟坐下）
//-- 2. 右前脚来回摆动（保持原始物理效果）
//-- 3. 右前脚回中立
//-- 4. 后两腿回中立
//--------------------------------------------------------------
void Dog::SayHello(int wave_times, int period, int amount) {
  ESP_LOGI(TAG, "打招呼: wave_times=%d, period=%d, amount=%d", wave_times, period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  // Real-world direction:
  // - Left legs: smaller angle = forward
  // - Right legs: larger angle = forward
  // This keeps the original physical effect: sit with both rear legs,
  // then wave the right front paw.
  const int neutral = kDogNeutralAngle;
  const int sit_amount = amount * 2;   // sit depth
  const int wave_amount = amount * 2;  // wave amplitude

  const int left_rear_sit = LeftForward(neutral, sit_amount);
  const int right_rear_sit = RightForward(neutral, sit_amount);
  const int wave_forward = RightForward(neutral, wave_amount);
  const int wave_back = RightForward(neutral, amount);

  auto move = [this](int duration_ms, const int target[SERVO_COUNT]) {
    MoveServosWithEase(duration_ms, const_cast<int*>(target), EASE_IN_OUT);
  };

  // 第1步：坐下（后腿向前摆，身体降低）
  {
    const int target[SERVO_COUNT] = {
        neutral,       // 左前腿：保持
        left_rear_sit, // 左后腿：向前（坐下）
        neutral,       // 右前腿：保持
        right_rear_sit // 右后腿：向前（坐下）
    };
    move(500, target);
  }

  vTaskDelay(pdMS_TO_TICKS(200));

  // 第2步：右前腿招手（其他腿保持坐下）
  for (int i = 0; i < wave_times; i++) {
    const int target_up[SERVO_COUNT] = {
        neutral,       // 左前腿：保持
        left_rear_sit, // 左后腿：保持坐下
        wave_forward,  // 右前腿：向前（抬起）
        right_rear_sit // 右后腿：保持坐下
    };
    move(period / 2, target_up);

    const int target_down[SERVO_COUNT] = {
        neutral,       // 左前腿：保持
        left_rear_sit, // 左后腿：保持坐下
        wave_back,     // 右前腿：回一点（放下）
        right_rear_sit // 右后腿：保持坐下
    };
    move(period / 2, target_down);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 第3步：右前腿回中立（结束招手）
  {
    const int target[SERVO_COUNT] = {
        neutral,       // 左前腿：中立
        left_rear_sit, // 左后腿：保持坐下
        neutral,       // 右前腿：中立
        right_rear_sit // 右后腿：保持坐下
    };
    move(300, target);
  }

  vTaskDelay(pdMS_TO_TICKS(200));

  // 第4步：站起（全部回中立）
  {
    const int target[SERVO_COUNT] = {
        neutral, // 左前腿：中立
        neutral, // 左后腿：中立
        neutral, // 右前腿：中立
        neutral  // 右后腿：中立
    };
    move(500, target);
  }

  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog好奇前倾 - 用整体前探模拟“凑近闻一下”
//--------------------------------------------------------------
void Dog::CuriousLean(int period, int amount) {
  ESP_LOGI(TAG, "好奇前倾: period=%d, amount=%d", period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  const int neutral = kDogNeutralAngle;
  const int rear_anchor = amount > 6 ? amount - 4 : amount;
  const int pulse_extra = 4;

  // 当前真实物理腿位:
  // [0]=左前, [1]=左后, [2]=右前, [3]=右后
  // 左侧腿角度减小=向前，右侧腿角度增大=向前
  const int lean_target[SERVO_COUNT] = {
      LeftForward(neutral, amount),        // 左前向前
      LeftBackward(neutral, rear_anchor),  // 左后向后支撑
      RightForward(neutral, amount),       // 右前向前
      RightBackward(neutral, rear_anchor)  // 右后向后支撑
  };
  MoveServosWithEase(period * 2 / 5, const_cast<int *>(lean_target), EASE_OUT);

  vTaskDelay(pdMS_TO_TICKS(90));

  const int sniff_target[SERVO_COUNT] = {
      LeftForward(neutral, amount + pulse_extra),   // 左前再前探一点
      LeftBackward(neutral, rear_anchor + 2),       // 左后微微后撑
      RightForward(neutral, amount + pulse_extra),  // 右前再前探一点
      RightBackward(neutral, rear_anchor + 2)       // 右后微微后撑
  };
  MoveServosWithEase(period / 5, const_cast<int *>(sniff_target), EASE_IN_OUT);
  MoveServosWithEase(period / 5, const_cast<int *>(lean_target), EASE_IN_OUT);
}

//--------------------------------------------------------------
//-- Dog受惊后缩 - 短促后收，模拟被碰到时缩一下
//--------------------------------------------------------------
void Dog::FlinchBack(int period, int amount) {
  ESP_LOGI(TAG, "受惊后缩: period=%d, amount=%d", period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  const int neutral = kDogNeutralAngle;
  const int rear_tuck = amount > 6 ? amount - 4 : amount;

  const int recoil_target[SERVO_COUNT] = {
      LeftBackward(neutral, amount),   // 左前向后缩
      LeftForward(neutral, rear_tuck), // 左后向前收
      RightBackward(neutral, amount),  // 右前向后缩
      RightForward(neutral, rear_tuck) // 右后向前收
  };
  MoveServosWithEase(period / 3, const_cast<int *>(recoil_target), EASE_OUT);

  vTaskDelay(pdMS_TO_TICKS(80));

  const int settle_target[SERVO_COUNT] = {
      LeftBackward(neutral, amount / 2),   // 左前半收
      LeftForward(neutral, rear_tuck / 2), // 左后半收
      RightBackward(neutral, amount / 2),  // 右前半收
      RightForward(neutral, rear_tuck / 2) // 右后半收
  };
  MoveServosWithEase(period / 2, const_cast<int *>(settle_target), EASE_IN_OUT);
}

//--------------------------------------------------------------
//-- Dog开心前趴 - 四条腿一起慢慢往后收，模仿邀玩姿态
//--------------------------------------------------------------
void Dog::PlayBow(int period, int amount) {
  ESP_LOGI(TAG, "开心前趴: period=%d, amount=%d", period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  const int neutral = kDogNeutralAngle;
  const int bow_target[SERVO_COUNT] = {
      LeftBackward(neutral, amount),  // 左前往后收
      LeftBackward(neutral, amount),  // 左后往后收
      RightBackward(neutral, amount), // 右前往后收
      RightBackward(neutral, amount)  // 右后往后收
  };
  int home_target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};

  MoveServosWithEase(period * 4 / 5, const_cast<int *>(bow_target), EASE_IN_OUT);
  vTaskDelay(pdMS_TO_TICKS(420));
  MoveServosWithEase(period / 3, home_target, EASE_IN_OUT);
}

//--------------------------------------------------------------
//-- Dog惊讶蓄力 - 前腿先快速内收，再带后腿跟进，最后快速复位
//--------------------------------------------------------------
void Dog::SurpriseJumpPrep(int period, int front_amount, int rear_amount) {
  ESP_LOGI(TAG,
           "惊讶蓄力: period=%d, front_amount=%d, rear_amount=%d", period,
           front_amount, rear_amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  const int neutral = kDogNeutralAngle;
  const int safe_front_amount = front_amount > 82 ? 82 : front_amount;
  const int safe_rear_amount = rear_amount > 28 ? 28 : rear_amount;

  const int front_tuck[SERVO_COUNT] = {
      LeftBackward(neutral, safe_front_amount), // 左前往里收到接近180
      neutral,                                  // 左后先不动
      RightBackward(neutral, safe_front_amount), // 右前往里收到接近0
      neutral                                    // 右后先不动
  };
  MoveServosWithEase(period * 2 / 5, const_cast<int *>(front_tuck), EASE_IN);

  vTaskDelay(pdMS_TO_TICKS(50));

  const int full_tuck[SERVO_COUNT] = {
      LeftBackward(neutral, safe_front_amount), // 左前保持内收
      LeftBackward(neutral, safe_rear_amount),  // 左后小幅后收
      RightBackward(neutral, safe_front_amount), // 右前保持内收
      RightBackward(neutral, safe_rear_amount)   // 右后小幅后收
  };
  MoveServosWithEase(period / 4, const_cast<int *>(full_tuck), EASE_IN);

  vTaskDelay(pdMS_TO_TICKS(35));

  int reset_target[SERVO_COUNT] = {neutral, neutral, neutral, neutral};
  MoveServosWithEase(period / 5, reset_target, EASE_OUT);
}

//--------------------------------------------------------------
//-- 使用缓动函数移动舵机
//--------------------------------------------------------------
void Dog::MoveServosWithEase(int time, int servo_target[], EaseType ease_type) {
  if (GetRestState() == true) {
    SetRestState(false);
  }

  if (time > 10) {
    int start_position[SERVO_COUNT];
    bool need_move[SERVO_COUNT];  // 标记哪些舵机需要移动
    
    for (int i = 0; i < SERVO_COUNT; i++) {
      start_position[i] = CurrentLogicalPosition(servo_[i], servo_trim_[i]);
      // 计算是否需要移动：如果起始位置和目标位置差异很小（<5度），则不需要移动
      // 舵机读取误差可能达到±2-3度，用5度阈值确保静止的腿不会抖动
      int delta = abs(servo_target[i] - start_position[i]);
      need_move[i] = (delta >= 5);
      
      // 对于不需要移动的舵机，直接设置目标位置，避免累积误差
      if (!need_move[i]) {
        servo_[i].SetPosition(servo_target[i] + servo_trim_[i]);
      }
    }

    unsigned long start_time = esp_timer_get_time() / 1000;
    unsigned long end_time = start_time + time;

    while (esp_timer_get_time() / 1000 < end_time) {
      float progress = (float)(esp_timer_get_time() / 1000 - start_time) / time;
      progress = fminf(progress, 1.0f);
      float eased = ApplyEasing(progress, ease_type);

      for (int i = 0; i < SERVO_COUNT; i++) {
        // 只移动需要移动的舵机
        if (need_move[i]) {
          int delta = servo_target[i] - start_position[i];
          servo_[i].SetPosition(start_position[i] + (int)(delta * eased) + servo_trim_[i]);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  // 确保到达最终位置
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].SetPosition(servo_target[i] + servo_trim_[i]);
  }
}

//--------------------------------------------------------------
//-- 多路径点轨迹运动
//--------------------------------------------------------------
void Dog::MoveServoPath(int servo_index, BezierWaypoint waypoints[], int count) {
  if (servo_index < 0 || servo_index >= SERVO_COUNT || count <= 0) {
    return;
  }

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  int current_position =
      CurrentLogicalPosition(servo_[servo_index], servo_trim_[servo_index]);

  for (int wp = 0; wp < count; wp++) {
    int target_position = waypoints[wp].position;
    int duration = waypoints[wp].duration_ms;
    EaseType ease = waypoints[wp].ease;

    unsigned long start_time = esp_timer_get_time() / 1000;
    unsigned long end_time = start_time + duration;

    while (esp_timer_get_time() / 1000 < end_time) {
      float progress = (float)(esp_timer_get_time() / 1000 - start_time) / duration;
      progress = fminf(progress, 1.0f);
      float eased = ApplyEasing(progress, ease);

      int delta = target_position - current_position;
      servo_[servo_index].SetPosition(current_position + (int)(delta * eased) +
                                      servo_trim_[servo_index]);
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    servo_[servo_index].SetPosition(target_position + servo_trim_[servo_index]);
    current_position = target_position;
  }
}

//--------------------------------------------------------------
//-- 启用舵机速度限制
//--------------------------------------------------------------
void Dog::EnableServoLimit(int speed_limit_degree_per_sec) {
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].SetLimiter(speed_limit_degree_per_sec);
  }
}

//--------------------------------------------------------------
//-- 禁用舵机速度限制
//--------------------------------------------------------------
void Dog::DisableServoLimit() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_[i].DisableLimiter();
  }
}
