#ifndef __DOG_MOVEMENTS_H__
#define __DOG_MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"

//-- Constants
#define FORWARD 1
#define BACKWARD -1
#define LEFT 1
#define RIGHT -1
#define BOTH 0
#define SMALL 5
#define MEDIUM 15
#define BIG 30

// -- Servo delta limit default. degree / sec
#define SERVO_LIMIT_DEFAULT 240

// -- Servo indexes for easy access (四足小狗)
#define LEFT_FRONT_LEG 0  // 左前腿 - GPIO 39
#define LEFT_REAR_LEG 1   // 左后腿 - GPIO 38
#define RIGHT_FRONT_LEG 2 // 右前腿 - GPIO 17
#define RIGHT_REAR_LEG 3  // 右后腿 - GPIO 18
#define SERVO_COUNT 4

// 预定义的运动曲线类型
enum EaseType {
  EASE_LINEAR = 0,      // 线性（无缓动）
  EASE_IN_OUT = 1,      // 标准 S 型（默认）
  EASE_IN = 2,          // 慢启动，快结束
  EASE_OUT = 3,         // 快启动，慢结束
  EASE_IN_BACK = 4,     // 回弹启动
  EASE_OUT_BACK = 5,    // 回弹结束
  EASE_OUT_BOUNCE = 6,  // 弹跳结束
};

// 贝塞尔路径点（用于多点轨迹）
struct BezierWaypoint {
  int position;      // 目标角度
  int duration_ms;   // 到达此点的时间
  EaseType ease;     // 缓动类型
};

// 最大路径点数
#define MAX_WAYPOINTS 8


class Dog {
public:
  Dog();
  ~Dog();

  //-- Dog initialization
  void Init(int left_rear_leg, int left_front_leg, int right_front_leg, int right_rear_leg);
  
  //-- Attach & detach functions
  void AttachServos();
  void DetachServos();

  //-- Oscillator Trims
  void SetTrims(int left_rear_leg, int left_front_leg, int right_front_leg, int right_rear_leg);

  //-- Predetermined Motion Functions
  void MoveServos(int time, int servo_target[]);
  void MoveSingle(int position, int servo_number);
  void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                       int period, double phase_diff[SERVO_COUNT], float cycle);

  //-- HOME = Dog at rest position
  void Home();
  void ForceHome();
  void SleepPose();
  bool GetRestState();
  void SetRestState(bool state);

  //-- Dog movement functions - 基于对角线蓄力的运动方式
  /**
   * 前进动作 - 使用对角线蓄力方法
   * @param steps 步数
   * @param period 每步周期（毫秒）
   * @param amount 步幅大小
   */
  void WalkForward(float steps = 8, int period = 1000, int amount = 30);
  
  /**
   * 后退动作 - 使用对角线蓄力方法
   * @param steps 步数
   * @param period 每步周期（毫秒）
   * @param amount 步幅大小
   */
  void WalkBackward(float steps = 8, int period = 1000, int amount = 30);

  /**
   * 前后摇摆 - 四条腿同时前后摆动
   * @param steps 摇摆次数
   * @param period 每次摇摆周期（毫秒）
   * @param amount 摆动幅度
   */
  void SwayBackForth(int steps = 5, int period = 1000, int amount = 30);

  /**
   * 俯卧撑 - 前腿同时向前后摆动，后腿保持中立
   * @param steps 次数
   * @param period 每次周期（毫秒）
   * @param amount 摆动幅度
   */
  void PushUp(int steps = 4, int period = 1000, int amount = 30);

  /**
   * 右转动作 - 四步对角线步态
   * @param steps 步数
   * @param period 每步周期（毫秒）
   * @param amount 步幅大小
   */
  void TurnRight(float steps = 8, int period = 1000, int amount = 30);

  /**
   * 左转动作 - 右转的逆序
   * @param steps 步数
   * @param period 每步周期（毫秒）
   * @param amount 步幅大小
   */
  void TurnLeft(float steps = 8, int period = 1000, int amount = 30);

  /**
   * 打招呼动作 - 模仿小狗招手
   * @param wave_times 招手次数
   * @param period 每次招手周期（毫秒）
   * @param amount 摆动幅度
   */
  void SayHello(int wave_times = 5, int period = 500, int amount = 30);

  // -- 贝塞尔曲线轨迹运动（更平滑）
  void MoveServosWithEase(int time, int servo_target[], EaseType ease_type);
  void MoveServoPath(int servo_index, BezierWaypoint waypoints[], int count);

  // -- Servo limiter
  void EnableServoLimit(int speed_limit_degree_per_sec = SERVO_LIMIT_DEFAULT);
  void DisableServoLimit();

private:
  Oscillator servo_[SERVO_COUNT];

  int servo_pins_[SERVO_COUNT];
  int servo_trim_[SERVO_COUNT];

  unsigned long final_time_;
  unsigned long partial_time_;
  float increment_[SERVO_COUNT];

  bool is_dog_resting_;

  // -- Advanced oscillation
  void Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
               double phase_diff[SERVO_COUNT], float steps);
};

#endif // __DOG_MOVEMENTS_H__
