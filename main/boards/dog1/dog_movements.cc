#include "dog_movements.h"
#include <math.h>
#include <string.h>

static const char *TAG = "DogMovements";

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
    for (int i = 0; i < SERVO_COUNT; i++) {
      increment_[i] = ((servo_target[i]) - servo_[i].GetPosition()) / (time / 10.0);
    }

    final_time_ = esp_timer_get_time() / 1000 + time;

    for (int iteration = 1; esp_timer_get_time() / 1000 < final_time_; iteration++) {
      partial_time_ = esp_timer_get_time() / 1000 + 10;
      for (int i = 0; i < SERVO_COUNT; i++) {
        servo_[i].SetPosition(servo_[i].GetPosition() + increment_[i]);
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
    servo_target[i] = servo_[i].GetPosition();
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
    DetachServos();
    is_dog_resting_ = true;
  }
}

void Dog::ForceHome() {
  int servo_position[SERVO_COUNT] = {90, 90, 90, 90};
  MoveServos(500, servo_position);
  DetachServos();
  is_dog_resting_ = true;
}

void Dog::SleepPose() {
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  int servo_position[SERVO_COUNT] = {0, 180, 0, 180};
  MoveServosWithEase(600, servo_position, EASE_IN_OUT);
  DetachServos();
  is_dog_resting_ = true;
}

bool Dog::GetRestState() { return is_dog_resting_; }

void Dog::SetRestState(bool state) { is_dog_resting_ = state; }

//--------------------------------------------------------------
//-- Dog前进动作 - 八步对角线步态
//-- 步态说明：
//-- 对角线组A: 右前腿(2) + 左后腿(0)
//-- 对角线组B: 左前腿(1) + 右后腿(3)
//-- 
//-- 注意：左侧和右侧舵机安装方向相反
//-- 左侧：角度增大 = 向前摆
//-- 右侧：角度减小 = 向前摆
//-- 
//-- 八步循环（一个完整周期）:
//-- 第1步: 组A（右前+左后）向前摆动
//-- 第2步: 组B（左前+右后）向后蹬
//-- 第3步: 组A还原到中立位
//-- 第4步: 组B还原到中立位
//-- 第5步: 组B（左前+右后）向前摆动
//-- 第6步: 组A（右前+左后）向后蹬
//-- 第7步: 组B还原到中立位
//-- 第8步: 组A还原到中立位
//--------------------------------------------------------------
void Dog::WalkForward(float steps, int period, int amount) {
  ESP_LOGI(TAG, "前进(八步对角线步态): steps=%.1f, period=%d, amount=%d", steps, period, amount);
  
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }
  
  // 每步的时间（八步一个周期）
  int step_time = period / 8;
  
  // 中立位置
  int neutral = 90;
  
  // 左侧腿：角度增大=向前，角度减小=向后
  int left_forward = neutral + amount;
  int left_backward = neutral - amount;
  
  // 右侧腿：角度减小=向前，角度增大=向后（镜像安装）
  int right_forward = neutral - amount;
  int right_backward = neutral + amount;
  
  // 执行指定步数
  for (int step = 0; step < (int)steps; step++) {
    // ========== 前四步：组A先动 ==========
    
    // 第1步: 对角线组A（右前+左后）向前摆动
    // 此时组B（左前+右后）支撑
    {
      int target[SERVO_COUNT] = {
        left_forward,   // [0] LEFT_REAR_LEG - 向前摆
        neutral,        // [1] LEFT_FRONT_LEG - 保持支撑
        right_forward,  // [2] RIGHT_FRONT_LEG - 向前摆
        neutral         // [3] RIGHT_REAR_LEG - 保持支撑
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第2步: 对角线组B（左前+右后）向后蹬（推进身体）
    {
      int target[SERVO_COUNT] = {
        left_forward,    // [0] LEFT_REAR_LEG - 保持前方位置
        left_backward,   // [1] LEFT_FRONT_LEG - 向后蹬
        right_forward,   // [2] RIGHT_FRONT_LEG - 保持前方位置
        right_backward   // [3] RIGHT_REAR_LEG - 向后蹬
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第3步: 对角线组A（右前+左后）还原到中立位
    {
      int target[SERVO_COUNT] = {
        neutral,         // [0] LEFT_REAR_LEG - 还原中立
        left_backward,   // [1] LEFT_FRONT_LEG - 保持后方位置
        neutral,         // [2] RIGHT_FRONT_LEG - 还原中立
        right_backward   // [3] RIGHT_REAR_LEG - 保持后方位置
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第4步: 对角线组B（左前+右后）还原到中立位
    {
      int target[SERVO_COUNT] = {
        neutral,   // [0] LEFT_REAR_LEG - 中立
        neutral,   // [1] LEFT_FRONT_LEG - 还原中立
        neutral,   // [2] RIGHT_FRONT_LEG - 中立
        neutral    // [3] RIGHT_REAR_LEG - 还原中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // ========== 后四步：组B先动 ==========
    
    // 第5步: 对角线组B（左前+右后）向前摆动
    // 此时组A（右前+左后）支撑
    {
      int target[SERVO_COUNT] = {
        neutral,        // [0] LEFT_REAR_LEG - 保持支撑
        left_forward,   // [1] LEFT_FRONT_LEG - 向前摆
        neutral,        // [2] RIGHT_FRONT_LEG - 保持支撑
        right_forward   // [3] RIGHT_REAR_LEG - 向前摆
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第6步: 对角线组A（右前+左后）向后蹬（推进身体）
    {
      int target[SERVO_COUNT] = {
        left_backward,   // [0] LEFT_REAR_LEG - 向后蹬
        left_forward,    // [1] LEFT_FRONT_LEG - 保持前方位置
        right_backward,  // [2] RIGHT_FRONT_LEG - 向后蹬
        right_forward    // [3] RIGHT_REAR_LEG - 保持前方位置
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第7步: 对角线组B（左前+右后）还原到中立位
    {
      int target[SERVO_COUNT] = {
        left_backward,   // [0] LEFT_REAR_LEG - 保持后方位置
        neutral,         // [1] LEFT_FRONT_LEG - 还原中立
        right_backward,  // [2] RIGHT_FRONT_LEG - 保持后方位置
        neutral          // [3] RIGHT_REAR_LEG - 还原中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第8步: 对角线组A（右前+左后）还原到中立位
    // 完成一个完整步态周期
    {
      int target[SERVO_COUNT] = {
        neutral,   // [0] LEFT_REAR_LEG - 还原中立
        neutral,   // [1] LEFT_FRONT_LEG - 中立
        neutral,   // [2] RIGHT_FRONT_LEG - 还原中立
        neutral    // [3] RIGHT_REAR_LEG - 中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 添加延迟避免看门狗超时
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
  
  // 每步的时间（八步一个周期）
  int step_time = period / 8;
  
  // 中立位置
  int neutral = 90;
  
  // 左侧腿：角度增大=向前，角度减小=向后
  int left_forward = neutral + amount;
  int left_backward = neutral - amount;
  
  // 右侧腿：角度减小=向前，角度增大=向后（镜像安装）
  int right_forward = neutral - amount;
  int right_backward = neutral + amount;
  
  // 执行指定步数
  for (int step = 0; step < (int)steps; step++) {
    // ========== 严格按照前进的逆序：8→7→6→5→4→3→2→1 ==========
    
    // 前进第8步的状态: neutral, neutral, neutral, neutral
    // 后退第1步(逆8): 从第7步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        left_backward,   // [0] LEFT_REAR_LEG - 保持后方位置
        neutral,         // [1] LEFT_FRONT_LEG - 还原中立
        right_backward,  // [2] RIGHT_FRONT_LEG - 保持后方位置
        neutral          // [3] RIGHT_REAR_LEG - 还原中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第7步的状态: left_backward, neutral, right_backward, neutral
    // 后退第2步(逆7): 从第6步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        left_backward,   // [0] LEFT_REAR_LEG - 向后蹬
        left_forward,    // [1] LEFT_FRONT_LEG - 保持前方位置
        right_backward,  // [2] RIGHT_FRONT_LEG - 向后蹬
        right_forward    // [3] RIGHT_REAR_LEG - 保持前方位置
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第6步的状态: left_backward, left_forward, right_backward, right_forward
    // 后退第3步(逆6): 从第5步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        neutral,        // [0] LEFT_REAR_LEG - 保持支撑
        left_forward,   // [1] LEFT_FRONT_LEG - 向前摆
        neutral,        // [2] RIGHT_FRONT_LEG - 保持支撑
        right_forward   // [3] RIGHT_REAR_LEG - 向前摆
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第5步的状态: neutral, left_forward, neutral, right_forward
    // 后退第4步(逆5): 从第4步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        neutral,   // [0] LEFT_REAR_LEG - 中立
        neutral,   // [1] LEFT_FRONT_LEG - 还原中立
        neutral,   // [2] RIGHT_FRONT_LEG - 中立
        neutral    // [3] RIGHT_REAR_LEG - 还原中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第4步的状态: neutral, neutral, neutral, neutral
    // 后退第5步(逆4): 从第3步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        neutral,         // [0] LEFT_REAR_LEG - 还原中立
        left_backward,   // [1] LEFT_FRONT_LEG - 保持后方位置
        neutral,         // [2] RIGHT_FRONT_LEG - 还原中立
        right_backward   // [3] RIGHT_REAR_LEG - 保持后方位置
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第3步的状态: neutral, left_backward, neutral, right_backward
    // 后退第6步(逆3): 从第2步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        left_forward,    // [0] LEFT_REAR_LEG - 保持前方位置
        left_backward,   // [1] LEFT_FRONT_LEG - 向后蹬
        right_forward,   // [2] RIGHT_FRONT_LEG - 保持前方位置
        right_backward   // [3] RIGHT_REAR_LEG - 向后蹬
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第2步的状态: left_forward, left_backward, right_forward, right_backward
    // 后退第7步(逆2): 从第1步的状态倒回去
    {
      int target[SERVO_COUNT] = {
        left_forward,   // [0] LEFT_REAR_LEG - 向前摆
        neutral,        // [1] LEFT_FRONT_LEG - 保持支撑
        right_forward,  // [2] RIGHT_FRONT_LEG - 向前摆
        neutral         // [3] RIGHT_REAR_LEG - 保持支撑
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 前进第1步的状态: left_forward, neutral, right_forward, neutral
    // 后退第8步(逆1): 还原到中立，完成一个周期
    {
      int target[SERVO_COUNT] = {
        neutral,   // [0] LEFT_REAR_LEG - 还原中立
        neutral,   // [1] LEFT_FRONT_LEG - 中立
        neutral,   // [2] RIGHT_FRONT_LEG - 还原中立
        neutral    // [3] RIGHT_REAR_LEG - 中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 添加延迟避免看门狗超时
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
  int neutral = 90;

  // 左侧腿：角度增大=向前，角度减小=向后
  int left_forward = neutral + amount;
  int left_backward = neutral - amount;

  // 右侧腿：角度减小=向前，角度增大=向后（镜像安装）
  int right_forward = neutral - amount;
  int right_backward = neutral + amount;

  for (int i = 0; i < steps; i++) {
    int target_back[SERVO_COUNT] = {
      left_backward,  // 左后腿
      left_backward,  // 左前腿
      right_backward, // 右前腿
      right_backward  // 右后腿
    };
    MoveServosWithEase(half_time, target_back, EASE_IN_OUT);

    int target_forward[SERVO_COUNT] = {
      left_forward,  // 左后腿
      left_forward,  // 左前腿
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
  int neutral = 90;

  int push_amount = amount * 2;

  // 左侧腿：角度增大=向前，角度减小=向后
  int left_forward = neutral + push_amount;

  // 右侧腿：角度减小=向前，角度增大=向后（镜像安装）
  int right_forward = neutral - push_amount;

  for (int i = 0; i < steps; i++) {
    int target_forward[SERVO_COUNT] = {
      left_forward,  // 左前腿向前
      neutral,       // 左后腿保持中立
      neutral,       // 右后腿保持中立
      right_forward  // 右前腿向前
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
  
  // 每步的时间（四步一个周期）
  int step_time = period / 4;
  
  // 中立位置
  int neutral = 90;
  
  // 左侧腿：角度增大=向前，角度减小=向后
  int left_forward = neutral + amount;
  int left_backward = neutral - amount;
  
  // 右侧腿：角度减小=向前，角度增大=向后（镜像安装）
  int right_forward = neutral - amount;
  int right_backward = neutral + amount;
  
  // 执行指定步数
  for (int step = 0; step < (int)steps; step++) {
    // ========== 逆序执行：从第4步到第1步 ==========
    
    // 第4步(逆): 组B准备（右后往前 + 左前往后）
    {
      int target[SERVO_COUNT] = {
        neutral,         // [0] LEFT_REAR_LEG - 中立
        left_backward,   // [1] LEFT_FRONT_LEG - 往后
        neutral,         // [2] RIGHT_FRONT_LEG - 中立
        right_forward    // [3] RIGHT_REAR_LEG - 往前
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第3步(逆): 组A准备（右前往后 + 左后往前）
    {
      int target[SERVO_COUNT] = {
        left_forward,    // [0] LEFT_REAR_LEG - 往前
        left_backward,   // [1] LEFT_FRONT_LEG - 保持后方
        right_backward,  // [2] RIGHT_FRONT_LEG - 往后
        right_forward    // [3] RIGHT_REAR_LEG - 保持前方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第2步(逆): 组B还原中立
    {
      int target[SERVO_COUNT] = {
        left_forward,    // [0] LEFT_REAR_LEG - 保持前方
        neutral,         // [1] LEFT_FRONT_LEG - 还原中立
        right_backward,  // [2] RIGHT_FRONT_LEG - 保持后方
        neutral          // [3] RIGHT_REAR_LEG - 还原中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第1步(逆): 组A还原中立，完成一个周期
    {
      int target[SERVO_COUNT] = {
        neutral,   // [0] LEFT_REAR_LEG - 还原中立
        neutral,   // [1] LEFT_FRONT_LEG - 中立
        neutral,   // [2] RIGHT_FRONT_LEG - 还原中立
        neutral    // [3] RIGHT_REAR_LEG - 中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 添加延迟避免看门狗超时
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog左转动作 - 四步对角线步态
//-- 步态说明：
//-- 对角线组A: 右前腿(2) + 左后腿(0)
//-- 对角线组B: 左前腿(1) + 右后腿(3)
//-- 
//-- 左转产生逆时针力矩：
//-- 第1步: 右前往后 + 左后往前（组A产生力矩）
//-- 第2步: 右后往前 + 左前往后（组B产生力矩）
//-- 第3步: 右前和左后还原中立
//-- 第4步: 右后和左前还原中立
//--------------------------------------------------------------
void Dog::TurnLeft(float steps, int period, int amount) {
  ESP_LOGI(TAG, "左转(四步对角线步态): steps=%.1f, period=%d, amount=%d", steps, period, amount);
  
  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }
  
  // 每步的时间（四步一个周期）
  int step_time = period / 4;
  
  // 中立位置
  int neutral = 90;
  
  // 左侧腿：角度增大=向前，角度减小=向后
  int left_forward = neutral + amount;
  int left_backward = neutral - amount;
  
  // 右侧腿：角度减小=向前，角度增大=向后（镜像安装）
  int right_forward = neutral - amount;
  int right_backward = neutral + amount;
  
  // 执行指定步数
  for (int step = 0; step < (int)steps; step++) {
    // 第1步: 右前往后 + 左后往前（组A产生力矩）
    // 此时组B（左前+右后）支撑
    {
      int target[SERVO_COUNT] = {
        left_forward,    // [0] LEFT_REAR_LEG - 往前
        neutral,         // [1] LEFT_FRONT_LEG - 支撑
        right_backward,  // [2] RIGHT_FRONT_LEG - 往后
        neutral          // [3] RIGHT_REAR_LEG - 支撑
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第2步: 右后往前 + 左前往后（组B产生力矩）
    // 此时组A（右前+左后）支撑
    {
      int target[SERVO_COUNT] = {
        left_forward,    // [0] LEFT_REAR_LEG - 保持前方
        left_backward,   // [1] LEFT_FRONT_LEG - 往后
        right_backward,  // [2] RIGHT_FRONT_LEG - 保持后方
        right_forward    // [3] RIGHT_REAR_LEG - 往前
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第3步: 右前和左后还原中立
    // 现在组A支撑
    {
      int target[SERVO_COUNT] = {
        neutral,         // [0] LEFT_REAR_LEG - 还原中立
        left_backward,   // [1] LEFT_FRONT_LEG - 保持后方
        neutral,         // [2] RIGHT_FRONT_LEG - 还原中立
        right_forward    // [3] RIGHT_REAR_LEG - 保持前方
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 第4步: 右后和左前还原中立
    // 完成一个周期
    {
      int target[SERVO_COUNT] = {
        neutral,   // [0] LEFT_REAR_LEG - 中立
        neutral,   // [1] LEFT_FRONT_LEG - 还原中立
        neutral,   // [2] RIGHT_FRONT_LEG - 中立
        neutral    // [3] RIGHT_REAR_LEG - 还原中立
      };
      MoveServosWithEase(step_time, target, EASE_IN_OUT);
    }
    
    // 添加延迟避免看门狗超时
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // 注意: Home()由controller统一调用,这里不调用
}

//--------------------------------------------------------------
//-- Dog打招呼动作 - 模仿小狗招手
//-- 步态说明：
//-- 1. 后两腿同时向前（模拟坐下）
//-- 2. 左前脚来回摆动（招手）
//-- 3. 左前脚回中立
//-- 4. 后两腿回中立
//--------------------------------------------------------------
void Dog::SayHello(int wave_times, int period, int amount) {
  ESP_LOGI(TAG, "打招呼: wave_times=%d, period=%d, amount=%d", wave_times, period, amount);

  AttachServos();
  if (GetRestState() == true) {
    SetRestState(false);
  }

  // --- 可调角度（单位：度）---
  // 方向说明（基于当前舵机安装方向）：
  // - 左侧：角度增大 = 腿向前摆（朝头部方向）
  // - 右侧：角度减小 = 腿向前摆（朝头部方向）
  // - 坐下：后腿向前摆，身体降低
  // - 招手：左前腿前后摆动
  const int neutral = 90;
  const int sit_amount = amount * 2;   // sit depth
  const int wave_amount = amount * 2;  // wave amplitude

  const int left_rear_sit = neutral + sit_amount;
  const int right_rear_sit = neutral - sit_amount;
  const int wave_forward = neutral + wave_amount;
  const int wave_back = neutral + amount;

  auto move = [this](int duration_ms, const int target[SERVO_COUNT]) {
    MoveServosWithEase(duration_ms, const_cast<int*>(target), EASE_IN_OUT);
  };

  // 第1步：坐下（后腿向前摆，身体降低）
  // [0] 左前腿, [1] 左后腿, [2] 右后腿, [3] 右前腿
  {
    const int target[SERVO_COUNT] = {
        neutral,       // 左前腿：保持
        left_rear_sit, // 左后腿：向前（坐下）
        right_rear_sit,// 右后腿：向前（坐下）
        neutral        // 右前腿：保持
    };
    move(500, target);
  }

  vTaskDelay(pdMS_TO_TICKS(200));

  // 第2步：左前腿招手（其他腿保持坐下）
  for (int i = 0; i < wave_times; i++) {
    const int target_up[SERVO_COUNT] = {
        wave_forward,  // 左前腿：向前（抬起）
        left_rear_sit, // 左后腿：保持坐下
        right_rear_sit,// 右后腿：保持坐下
        neutral        // 右前腿：保持
    };
    move(period / 2, target_up);

    const int target_down[SERVO_COUNT] = {
        wave_back,     // 左前腿：回一点（放下）
        left_rear_sit, // 左后腿：保持坐下
        right_rear_sit,// 右后腿：保持坐下
        neutral        // 右前腿：保持
    };
    move(period / 2, target_down);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 第3步：左前腿回中立（结束招手）
  {
    const int target[SERVO_COUNT] = {
        neutral,       // 左前腿：中立
        left_rear_sit, // 左后腿：保持坐下
        right_rear_sit,// 右后腿：保持坐下
        neutral        // 右前腿：保持
    };
    move(300, target);
  }

  vTaskDelay(pdMS_TO_TICKS(200));

  // 第4步：站起（全部回中立）
  {
    const int target[SERVO_COUNT] = {
        neutral, // 左前腿：中立
        neutral, // 左后腿：中立
        neutral, // 右后腿：中立
        neutral  // 右前腿：中立
    };
    move(500, target);
  }

  // 注意: Home()由controller统一调用,这里不调用
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
      start_position[i] = servo_[i].GetPosition();
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

  int current_position = servo_[servo_index].GetPosition();

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
