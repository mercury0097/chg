#ifndef __PALQIQI_MOVEMENTS_H__
#define __PALQIQI_MOVEMENTS_H__

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

// -- Servo indexes for easy access
#define LEFT_LEG 0
#define RIGHT_LEG 1
#define LEFT_FOOT 2
#define RIGHT_FOOT 3
#define LEFT_HAND 4
#define RIGHT_HAND 5
#define SERVO_COUNT 6

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


class Palqiqi {
public:
  Palqiqi();
  ~Palqiqi();

  //-- Palqiqi initialization
  void Init(int left_leg, int right_leg, int left_foot, int right_foot,
            int left_hand = -1, int right_hand = -1);
  //-- Attach & detach functions
  void AttachServos();
  void DetachServos();

  //-- Oscillator Trims
  void SetTrims(int left_leg, int right_leg, int left_foot, int right_foot,
                int left_hand = 0, int right_hand = 0);

  //-- Predetermined Motion Functions
  void MoveServos(int time, int servo_target[]);
  void MoveSingle(int position, int servo_number);
  void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                       int period, double phase_diff[SERVO_COUNT], float cycle);

  //-- HOME = Palqiqi at rest position
  void Home(bool hands_down = true);
  bool GetRestState();
  void SetRestState(bool state);

  //-- Predetermined Motion Functions
  void Jump(float steps = 1, int period = 5000); // 增加到 5000ms，慢动作跳跃

  void Walk(float steps = 4, int period = 1000, int dir = FORWARD,
            int amount = 0);
  void Turn(float steps = 4, int period = 2000, int dir = LEFT, int amount = 0);
  void Bend(int steps = 1, int period = 1400, int dir = LEFT);
  void ShakeLeg(int steps = 1, int period = 2000, int dir = RIGHT);
  void LookAround(int period = 1000, int dir = LEFT);  // 左右看

  void UpDown(float steps = 1, int period = 1000, int height = 20);
  void Swing(float steps = 1, int period = 1000, int height = 20);
  void TiptoeSwing(float steps = 1, int period = 900, int height = 20);
  void Jitter(float steps = 1, int period = 500, int height = 20);
  void AscendingTurn(float steps = 1, int period = 900, int height = 20);

  void Moonwalker(float steps = 1, int period = 900, int height = 20,
                  int dir = LEFT);
  void Crusaito(float steps = 1, int period = 900, int height = 20,
                int dir = FORWARD);
  void Flapping(float steps = 1, int period = 1000, int height = 20,
                int dir = FORWARD);

  // -- 手部动作
  void HandsUp(int period = 1000, int dir = 0);     // 双手举起
  void HandsDown(int period = 1000, int dir = 0);   // 双手放下
  void HandWave(int period = 1000, int dir = LEFT); // 挥手
  void HandWaveBoth(int period = 1000);             // 双手同时挥手

  // -- 贝塞尔曲线轨迹运动（更平滑）
  void MoveServosWithEase(int time, int servo_target[], EaseType ease_type);
  void MoveServoPath(int servo_index, BezierWaypoint waypoints[], int count);
  void HandWaveSmooth(int period = 1000, int dir = LEFT); // 平滑挥手（带回弹）
  void JumpBounce(int period = 2000);                     // 弹跳跳跃

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

  bool is_palqiqi_resting_;
  bool has_hands_; // 是否有手部舵机

  // -- Advanced oscillation
  void Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
               double phase_diff[SERVO_COUNT], float steps);
};

#endif // __PALQIQI_MOVEMENTS_H__
