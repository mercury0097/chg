#include "palqiqi_movements.h"

#include <algorithm>
#include <cmath>

#include "oscillator.h"

static const char *TAG = "PalqiqiMovements";

#define HAND_HOME_POSITION 170  // 左手放下位置（右手为 180-170=10）
#define LEFT_LEG_HOME  95       // 左腿初始角度
#define RIGHT_LEG_HOME 90       // 右腿初始角度
#define LEFT_FOOT_HOME 85       // 左脚初始角度
#define RIGHT_FOOT_HOME 90      // 右脚初始角度

// 三次贝塞尔 S 型曲线查找表（33 个点，0.0 到 1.0）
// 控制点: P0=(0,0), P1=(0.25,0), P2=(0.75,1), P3=(1,1)
// 特点: 比余弦曲线更平滑，起止速度为0，中间加速更自然
static const float EASE_BEZIER_LUT[33] = {
    0.000f, 0.001f, 0.007f, 0.020f, 0.042f, 0.074f, 0.117f, 0.170f, 0.234f,
    0.305f, 0.383f, 0.461f, 0.537f, 0.609f, 0.676f, 0.736f, 0.789f, 0.836f,
    0.875f, 0.908f, 0.934f, 0.955f, 0.971f, 0.982f, 0.990f, 0.995f, 0.998f,
    0.999f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f};

// 三次贝塞尔曲线实时计算（用于需要精确控制的场景）
// 控制点可调: p1 控制起始加速度, p2 控制结束减速度
// ease-in-out 默认: p1=0.25, p2=0.75
inline float CubicBezier(float t, float p1 = 0.25f, float p2 = 0.75f) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  // 简化的三次贝塞尔: B(t) = 3(1-t)²t·p1 + 3(1-t)t²·p2 + t³
  float t2 = t * t;
  float t3 = t2 * t;
  float mt = 1.0f - t;
  float mt2 = mt * mt;
  return 3.0f * mt2 * t * p1 + 3.0f * mt * t2 * p2 + t3;
}

// 快速查找贝塞尔 S 型进度（CPU 优化：查表）
inline float GetEaseProgress(float progress) {
  if (progress <= 0.0f)
    return 0.0f;
  if (progress >= 1.0f)
    return 1.0f;
  int index = (int)(progress * 32.0f);
  if (index > 32)
    index = 32;
  return EASE_BEZIER_LUT[index];
}

// 可调节的 S 型曲线（支持不同的加减速特性）
// smoothness: 0.0=线性, 0.5=标准S型, 1.0=极端S型（几乎阶跃）
inline float GetEaseProgressCustom(float progress, float smoothness = 0.5f) {
  if (progress <= 0.0f) return 0.0f;
  if (progress >= 1.0f) return 1.0f;
  float p1 = 0.5f - smoothness * 0.5f;  // 0.25 for smoothness=0.5
  float p2 = 0.5f + smoothness * 0.5f;  // 0.75 for smoothness=0.5
  return CubicBezier(progress, p1, p2);
}


///////////////////////////////////////////////////////////////////
//-- 贝塞尔曲线轨迹规划器 ----------------------------------------//
///////////////////////////////////////////////////////////////////

// 2D 点结构（用于轨迹规划）
struct Point2D {
  float x;  // 时间归一化 [0, 1]
  float y;  // 位置归一化 [0, 1]
};

// 三次贝塞尔曲线轨迹点计算
// P0=起点, P1=控制点1, P2=控制点2, P3=终点
inline Point2D BezierPoint(float t, Point2D p0, Point2D p1, Point2D p2,
                           Point2D p3) {
  float mt = 1.0f - t;
  float mt2 = mt * mt;
  float mt3 = mt2 * mt;
  float t2 = t * t;
  float t3 = t2 * t;

  Point2D result;
  result.x = mt3 * p0.x + 3.0f * mt2 * t * p1.x + 3.0f * mt * t2 * p2.x +
             t3 * p3.x;
  result.y = mt3 * p0.y + 3.0f * mt2 * t * p1.y + 3.0f * mt * t2 * p2.y +
             t3 * p3.y;
  return result;
}

// 根据曲线类型获取进度值
inline float GetEaseByType(float t, EaseType type) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;

  switch (type) {
    case EASE_LINEAR:
      return t;

    case EASE_IN_OUT:
      return GetEaseProgress(t);  // 使用查找表

    case EASE_IN: {
      // 慢启动: P1=(0.42, 0), P2=(1, 1)
      return CubicBezier(t, 0.0f, 0.0f);
    }

    case EASE_OUT: {
      // 快启动慢结束: P1=(0, 0), P2=(0.58, 1)
      return CubicBezier(t, 1.0f, 1.0f);
    }

    case EASE_IN_BACK: {
      // 回弹启动（先后退再前进）
      float s = 1.70158f;
      return t * t * ((s + 1.0f) * t - s);
    }

    case EASE_OUT_BACK: {
      // 回弹结束（超过目标再回来）
      float s = 1.70158f;
      float t1 = t - 1.0f;
      return t1 * t1 * ((s + 1.0f) * t1 + s) + 1.0f;
    }

    case EASE_OUT_BOUNCE: {
      // 弹跳效果
      if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
      } else if (t < 2.0f / 2.75f) {
        float t1 = t - 1.5f / 2.75f;
        return 7.5625f * t1 * t1 + 0.75f;
      } else if (t < 2.5f / 2.75f) {
        float t1 = t - 2.25f / 2.75f;
        return 7.5625f * t1 * t1 + 0.9375f;
      } else {
        float t1 = t - 2.625f / 2.75f;
        return 7.5625f * t1 * t1 + 0.984375f;
      }
    }

    default:
      return GetEaseProgress(t);
  }
}

Palqiqi::Palqiqi() {
  is_palqiqi_resting_ = false;
  has_hands_ = false;
  // 初始化所有舵机管脚为-1（未连接）
  for (int i = 0; i < SERVO_COUNT; i++) {
    servo_pins_[i] = -1;
    servo_trim_[i] = 0;
  }
}

Palqiqi::~Palqiqi() { DetachServos(); }

unsigned long IRAM_ATTR millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

void Palqiqi::Init(int left_leg, int right_leg, int left_foot, int right_foot,
                int left_hand, int right_hand) {
  servo_pins_[LEFT_LEG] = left_leg;
  servo_pins_[RIGHT_LEG] = right_leg;
  servo_pins_[LEFT_FOOT] = left_foot;
  servo_pins_[RIGHT_FOOT] = right_foot;
  servo_pins_[LEFT_HAND] = left_hand;
  servo_pins_[RIGHT_HAND] = right_hand;

  // 检查是否有手部舵机
  has_hands_ = (left_hand != -1 && right_hand != -1);

  AttachServos();
  is_palqiqi_resting_ = false;
}

///////////////////////////////////////////////////////////////////
//-- ATTACH & DETACH FUNCTIONS ----------------------------------//
///////////////////////////////////////////////////////////////////
void Palqiqi::AttachServos() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].Attach(servo_pins_[i]);
    }
  }
}

void Palqiqi::DetachServos() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].Detach();
    }
  }
}

///////////////////////////////////////////////////////////////////
//-- OSCILLATORS TRIMS ------------------------------------------//
///////////////////////////////////////////////////////////////////
void Palqiqi::SetTrims(int left_leg, int right_leg, int left_foot, int right_foot,
                    int left_hand, int right_hand) {
  servo_trim_[LEFT_LEG] = left_leg;
  servo_trim_[RIGHT_LEG] = right_leg;
  servo_trim_[LEFT_FOOT] = left_foot;
  servo_trim_[RIGHT_FOOT] = right_foot;

  if (has_hands_) {
    servo_trim_[LEFT_HAND] = left_hand;
    servo_trim_[RIGHT_HAND] = right_hand;
  }

  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].SetTrim(servo_trim_[i]);
    }
  }
}


///////////////////////////////////////////////////////////////////
//-- BASIC MOTION FUNCTIONS -------------------------------------//
///////////////////////////////////////////////////////////////////
void Palqiqi::MoveServos(int time, int servo_target[]) {
  if (GetRestState() == true) {
    SetRestState(false);
  }

  final_time_ = millis() + time;
  if (time > 10) {
    // 记录起始位置（用于余弦插值）
    int start_positions[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (servo_pins_[i] != -1) {
        start_positions[i] = servo_[i].GetPosition();
      }
    }

    // 使用余弦插值（仅对慢速动作 time > 100ms，减少 CPU 开销）
    bool use_ease = (time > 100);

    unsigned long start_time = millis();
    while (millis() < final_time_) {
      unsigned long elapsed = millis() - start_time;
      float progress = (float)elapsed / (float)time;

      // 获取平滑进度（查表，无三角函数计算）
      float ease_progress = use_ease ? GetEaseProgress(progress) : progress;

      // 更新所有舵机位置
      for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
          int new_pos = start_positions[i] +
                        (servo_target[i] - start_positions[i]) * ease_progress;
          servo_[i].SetPosition(new_pos);
        }
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }
  } else {
    // 快速动作直接设置（无插值）
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (servo_pins_[i] != -1) {
        servo_[i].SetPosition(servo_target[i]);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(time));
  }

  // final adjustment to the target.
  bool f = true;
  int adjustment_count = 0;
  while (f && adjustment_count < 10) {
    f = false;
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (servo_pins_[i] != -1 && servo_target[i] != servo_[i].GetPosition()) {
        f = true;
        break;
      }
    }
    if (f) {
      for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
          servo_[i].SetPosition(servo_target[i]);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      adjustment_count++;
    }
  };
}

void Palqiqi::MoveSingle(int position, int servo_number) {
  if (position > 180)
    position = 90;
  if (position < 0)
    position = 90;

  if (GetRestState() == true) {
    SetRestState(false);
  }

  if (servo_number >= 0 && servo_number < SERVO_COUNT &&
      servo_pins_[servo_number] != -1) {
    servo_[servo_number].SetPosition(position);
  }
}

void Palqiqi::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                           int period, double phase_diff[SERVO_COUNT],
                           float cycle = 1) {
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].SetO(offset[i]);
      servo_[i].SetA(amplitude[i]);
      servo_[i].SetT(period);
      servo_[i].SetPh(phase_diff[i]);
    }
  }

  double ref = millis();
  double end_time = period * cycle + ref;

  while (millis() < end_time) {
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (servo_pins_[i] != -1) {
        servo_[i].Refresh();
      }
    }
    vTaskDelay(5);
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}

void Palqiqi::Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT],
                   int period, double phase_diff[SERVO_COUNT],
                   float steps = 1.0) {
  if (GetRestState() == true) {
    SetRestState(false);
  }

  // 直接使用传入的步数，不再向上取整
  // 这样可以精确控制步数
  int cycles = (int)(steps + 0.5f);  // 四舍五入
  if (cycles < 1) cycles = 1;

  //-- Execute complete cycles
  for (int i = 0; i < cycles; i++) {
    OscillateServos(amplitude, offset, period, phase_diff);
  }

  // 平滑归位（400ms），修正位置偏差
  // 时间适中：太快会不稳，太慢会打断连续动作
  int final_positions[SERVO_COUNT];
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (i == LEFT_LEG || i == RIGHT_LEG || i == LEFT_FOOT || i == RIGHT_FOOT) {
      final_positions[i] = 90 + offset[i];
    } else {
      final_positions[i] = servo_[i].GetPosition();
    }
  }
  MoveServos(400, final_positions);
}

///////////////////////////////////////////////////////////////////
//-- HOME = Palqiqi at rest position ----------------------------//
///////////////////////////////////////////////////////////////////
void Palqiqi::Home(bool hands_down) {
  if (is_palqiqi_resting_ == false) { // Go to rest position only if necessary
    // 为所有舵机准备初始位置值
    int homes[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (i == LEFT_HAND || i == RIGHT_HAND) {
        if (hands_down) {
          // 如果需要复位手部，设置为默认值
          // 修正：右手安装方向相反，放下位置与左手相同
          homes[i] = HAND_HOME_POSITION;
        } else {
          // 如果不需要复位手部，保持当前位置
          homes[i] = servo_[i].GetPosition();
        }
      } else if (i == LEFT_LEG) {
        homes[i] = LEFT_LEG_HOME;
      } else if (i == RIGHT_LEG) {
        homes[i] = RIGHT_LEG_HOME;
      } else if (i == LEFT_FOOT) {
        homes[i] = LEFT_FOOT_HOME;
      } else if (i == RIGHT_FOOT) {
        homes[i] = RIGHT_FOOT_HOME;
      } else {
        homes[i] = 90;
      }
    }

    MoveServos(500, homes);
    is_palqiqi_resting_ = true;
  }

  vTaskDelay(pdMS_TO_TICKS(200));
}

bool Palqiqi::GetRestState() { return is_palqiqi_resting_; }

void Palqiqi::SetRestState(bool state) { is_palqiqi_resting_ = state; }


///////////////////////////////////////////////////////////////////
//-- PREDETERMINED MOTION SEQUENCES -----------------------------//
///////////////////////////////////////////////////////////////////
//-- Palqiqi movement: Jump
//--  Parameters:
//--    steps: Number of steps
//--    T: Period (默认 5000ms，慢动作跳跃)
//--  优化：使用贝塞尔曲线，增加准备和落地缓冲阶段
//---------------------------------------------------------
void Palqiqi::Jump(float steps, int period) {
  int homes[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};
  
  // 阶段1: 准备蓄力（下蹲）- 使用 EASE_IN_OUT 平滑下蹲，30%时间
  int crouch[SERVO_COUNT] = {
      90, 90, 120, 60, HAND_HOME_POSITION, HAND_HOME_POSITION};
  MoveServosWithEase(period * 0.30, crouch, EASE_IN_OUT);
  
  // 阶段2: 上跳（有力但不急）- 使用 EASE_OUT，15%时间
  int up[SERVO_COUNT] = {
      90, 90, 140, 40, HAND_HOME_POSITION, HAND_HOME_POSITION};
  MoveServosWithEase(period * 0.15, up, EASE_OUT);
  
  // 阶段3: 滞空（保持一会儿）- 10%时间
  vTaskDelay(pdMS_TO_TICKS(period * 0.10));
  
  // 阶段4: 下落（自然下落）- 使用 EASE_IN 模拟重力，15%时间
  int land[SERVO_COUNT] = {
      90, 90, 110, 70, HAND_HOME_POSITION, HAND_HOME_POSITION};
  MoveServosWithEase(period * 0.15, land, EASE_IN);
  
  // 阶段5: 落地缓冲（吸收冲击）- 使用 EASE_IN_OUT 平滑归位，30%时间
  MoveServosWithEase(period * 0.30, homes, EASE_IN_OUT);
}

//---------------------------------------------------------
//-- Palqiqi gait: Walking  (forward or backward)
//--  Parameters:
//--    * steps:  Number of steps
//--    * T : Period
//--    * Dir: Direction: FORWARD / BACKWARD
//--    * amount: 手部摆动幅度, 0表示不摆动
//---------------------------------------------------------
void Palqiqi::Walk(float steps, int period, int dir, int amount) {
  //-- Oscillator parameters for walking
  //-- 小步快走策略：降低振幅，提高稳定性
  
  // 振幅：降低到20度，避免舵机过载（咔咔声）
  // 左右对称，直线行走
  int A[SERVO_COUNT] = {20, 20, 20, 20, 0, 0};
  
  // 偏移：脚部微微踮起，增加稳定性
  int O[SERVO_COUNT] = {0,
                        0,
                        4,
                        -4,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90};
  
  // 相位差：髋关节同相(0)，脚关节与髋差90度
  // 注意：dir 取反以适配舵机物理安装方向（旋转180度安装）
  double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-dir * 90), DEG2RAD(-dir * 90),
                                    0, 0};

  // 手部摆动
  if (amount > 0 && has_hands_) {
    A[LEFT_HAND] = amount;
    A[RIGHT_HAND] = amount;
    phase_diff[LEFT_HAND] = phase_diff[RIGHT_LEG];
    phase_diff[RIGHT_HAND] = phase_diff[LEFT_LEG];
  } else {
    A[LEFT_HAND] = 0;
    A[RIGHT_HAND] = 0;
  }

  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi gait: Turning (left or right)
//--  Parameters:
//--   * Steps: Number of steps
//--   * T: Period
//--   * Dir: Direction: LEFT / RIGHT
//--   * amount: 手部摆动幅度, 0表示不摆动
//---------------------------------------------------------
void Palqiqi::Turn(float steps, int period, int dir, int amount) {
  //-- Same coordination than for walking (see Palqiqi::walk)
  //-- The Amplitudes of the hip's oscillators are not igual
  //-- When the right hip servo amplitude is higher, the steps taken by
  //--   the right leg are bigger than the left. So, the robot describes an
  //--   left arc
  //-- v12优化：降低振幅从30到20，避免舵机过载发出咔咔声
  int A[SERVO_COUNT] = {20, 20, 20, 20, 0, 0};
  int O[SERVO_COUNT] = {0,
                        0,
                        5,
                        -5,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90}; // 右手偏移修正
  double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(-90), 0, 0};

  if (dir == LEFT) {
    A[0] = 20; //-- Left hip servo (降低振幅)
    A[1] = 0;  //-- Right hip servo
  } else {
    A[0] = 0;
    A[1] = 20; // 降低振幅
  }

  // 如果amount>0且有手部舵机，设置手部振幅和相位
  if (amount > 0 && has_hands_) {
    // 手臂振幅使用传入的amount参数
    A[LEFT_HAND] = amount;
    A[RIGHT_HAND] = amount;

    // 转向时手臂摆动相位：左手与左腿同相，右手与右腿同相，增强转向效果
    phase_diff[LEFT_HAND] = phase_diff[LEFT_LEG];   // 左手与左腿同相
    phase_diff[RIGHT_HAND] = phase_diff[RIGHT_LEG]; // 右手与右腿同相
  } else {
    A[LEFT_HAND] = 0;
    A[RIGHT_HAND] = 0;
  }

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi gait: Lateral bend
//--  Parameters:
//--    steps: Number of bends
//--    T: Period of one bend
//--    dir: RIGHT=Right bend LEFT=Left bend
//--  使用贝塞尔曲线平滑过渡，速度放慢保持稳定
//---------------------------------------------------------
void Palqiqi::Bend(int steps, int period, int dir) {
  // Parameters of all the movements. Default: Left bend
  // [2]=左脚, [3]=右脚
  // 参考抖脚动作：左脚<90向外，>90向内；右脚<90向内，>90向外
  // 向左弯腰：左脚向外支撑(<90)，右脚向外抬起(>90)
  int bend1[SERVO_COUNT] = {
      90, 90, 62, 118, HAND_HOME_POSITION, HAND_HOME_POSITION};
  int bend2[SERVO_COUNT] = {
      90, 90, 62, 145, HAND_HOME_POSITION, HAND_HOME_POSITION};
  int homes[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};

  // Changes in the parameters if right direction is chosen
  // 向右弯腰：左脚向内抬起(>90)，右脚向内支撑(<90)
  if (dir == -1) {
    bend1[2] = 118;  // 左脚向内
    bend1[3] = 62;   // 右脚向内支撑
    bend2[2] = 145;  // 左脚向内更多
    bend2[3] = 62;   // 右脚保持支撑
  }

  // 放慢速度，使用贝塞尔曲线平滑过渡
  // T2 从 800ms 增加到 1200ms，更稳定
  int T2 = 1200;

  // Bend movement with bezier easing
  for (int i = 0; i < steps; i++) {
    // 准备姿势：使用 EASE_IN_OUT 平滑启动和停止
    MoveServosWithEase(T2 / 2, bend1, EASE_IN_OUT);
    // 弯腰：使用 EASE_IN_OUT 保持平滑
    MoveServosWithEase(T2 / 2, bend2, EASE_IN_OUT);
    // 保持弯腰姿势
    vTaskDelay(pdMS_TO_TICKS(period * 0.6));
    // 回到站立：使用 EASE_IN_OUT 平滑归位，时间延长到 800ms
    MoveServosWithEase(800, homes, EASE_IN_OUT);
  }
}


//---------------------------------------------------------
//-- Palqiqi gait: Shake a leg
//--  Parameters:
//--    steps: Number of shakes
//--    T: Period of one shake
//--    dir: RIGHT=Right leg LEFT=Left leg
//-- v12优化：降低幅度和速度，避免机器人倒下
//-- 舵机索引: [0]=左腿, [1]=右腿, [2]=左脚, [3]=右脚
//---------------------------------------------------------
void Palqiqi::ShakeLeg(int steps, int period, int dir) {
  int numberLegMoves = 2;

  // 默认: 右腿抖动 (dir=-1)
  // [2]=左脚(支撑), [3]=右脚(抬起抖动)
  int shake_leg1[SERVO_COUNT] = {
      90, 90, 60, 50, HAND_HOME_POSITION, HAND_HOME_POSITION};
  int shake_leg2[SERVO_COUNT] = {
      90, 90, 60, 130, HAND_HOME_POSITION, HAND_HOME_POSITION};
  int shake_leg3[SERVO_COUNT] = {
      90, 90, 60, 100, HAND_HOME_POSITION, HAND_HOME_POSITION};
  int homes[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};

  // 左腿抖动 (dir=1): 左脚抬起抖动，右脚支撑
  // 左脚方向反转 (180-x)，右脚支撑保持原方向
  if (dir == 1) {
    shake_leg1[2] = 130;  // 左脚抬起准备
    shake_leg1[3] = 120;         // 右脚支撑
    shake_leg2[2] = 50;   // 左脚抬高
    shake_leg2[3] = 120;         // 右脚保持支撑
    shake_leg3[2] = 80;  // 左脚抖动位置
    shake_leg3[3] = 120;         // 右脚保持支撑
  }

  int T2 = 2000;
  period = period - T2;
  period = (period > 400 * numberLegMoves) ? period : (400 * numberLegMoves);

  for (int j = 0; j < steps; j++) {
    // 阶段1: 慢慢移重心到支撑脚（两头都慢）
    MoveServosWithEase(T2 / 2, shake_leg1, EASE_IN_OUT);
    // 等待重心稳定
    vTaskDelay(pdMS_TO_TICKS(300));
    // 阶段2: 抬起抖动脚（两头都慢）
    MoveServosWithEase(T2 / 2, shake_leg2, EASE_IN_OUT);

    for (int i = 0; i < numberLegMoves; i++) {
      MoveServos(period / (2 * numberLegMoves), shake_leg3);
      MoveServos(period / (2 * numberLegMoves), shake_leg2);
    }
    // 回位时间延长到1500ms，使用平滑过渡，避免重心不稳
    MoveServosWithEase(2000, homes, EASE_IN_OUT);
  }

  vTaskDelay(pdMS_TO_TICKS(200));
}

//---------------------------------------------------------
//-- Palqiqi movement: Look around (left or right)
//--  Parameters:
//--    period: 动作周期(ms)
//--    dir: 方向 LEFT=1 向左看, RIGHT=-1 向右看
//--  动作说明：
//--    1. 两腿同时向一侧旋转
//--    2. 对侧脚作为重心，同侧脚微微抬起
//--    3. 造成身体转向看的视觉效果
//---------------------------------------------------------
void Palqiqi::LookAround(int period, int dir) {
  // [0]=左腿, [1]=右腿, [2]=左脚, [3]=右脚
  int homes[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};

  // 腿部旋转角度（髋关节）
  int leg_turn = 25;  // 旋转25度（加大幅度）
  // 脚部抬起角度（比抖腿小）
  int foot_lift = 20;  // 微微抬起20度（加大幅度）
  // 支撑脚倾斜角度
  int support_tilt = 15;  // 支撑倾斜15度（加大幅度）

  int look_pose[SERVO_COUNT];

  if (dir == 1) {
    // 向左看：两腿向左转，右脚支撑，左脚微抬
    look_pose[0] = 90 - leg_turn;      // 左腿向左转
    look_pose[1] = 90 - leg_turn;      // 右腿向左转
    look_pose[2] = 90 + foot_lift;     // 左脚微微抬起
    look_pose[3] = 90 - support_tilt;  // 右脚支撑（向内倾斜）
    look_pose[4] = HAND_HOME_POSITION;
    look_pose[5] = HAND_HOME_POSITION;
  } else {
    // 向右看：两腿向右转，左脚支撑，右脚微抬
    look_pose[0] = 90 + leg_turn;      // 左腿向右转
    look_pose[1] = 90 + leg_turn;      // 右腿向右转
    look_pose[2] = 90 + support_tilt;  // 左脚支撑（向内倾斜）
    look_pose[3] = 90 - foot_lift;     // 右脚微微抬起
    look_pose[4] = HAND_HOME_POSITION;
    look_pose[5] = HAND_HOME_POSITION;
  }

  // 转向看：使用平滑过渡（速度放慢）
  MoveServosWithEase(period * 2 / 3, look_pose, EASE_IN_OUT);

  // 保持姿势一小段时间
  vTaskDelay(pdMS_TO_TICKS(period / 3));

  // 转回原位（速度放慢）
  MoveServosWithEase(period * 2 / 3, homes, EASE_IN_OUT);
}

//---------------------------------------------------------
//-- Palqiqi movement: up & down
//--  Parameters:
//--    * steps: Number of jumps
//--    * T: Period
//--    * h: Jump height: SMALL / MEDIUM / BIG
//--              (or a number in degrees 0 - 90)
//---------------------------------------------------------
void Palqiqi::UpDown(float steps, int period, int height) {
  //-- Both feet are 180 degrees out of phase
  //-- Feet amplitude and offset are the same
  //-- Initial phase for the right foot is -90, so that it starts
  //--   in one extreme position (not in the middle)
  int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
  int O[SERVO_COUNT] = {
      0, 0, height, -height, HAND_HOME_POSITION, HAND_HOME_POSITION};
  double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(90), 0, 0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi movement: swinging side to side
//--  Parameters:
//--     steps: Number of steps
//--     T : Period
//--     h : Amount of swing (from 0 to 50 aprox)
//---------------------------------------------------------
void Palqiqi::Swing(float steps, int period, int height) {
  //-- Both feets are in phase. The offset is half the amplitude
  //-- It causes the robot to swing from side to side
  int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
  int O[SERVO_COUNT] = {0,
                        0,
                        height / 2,
                        -height / 2,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90};
  double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(0), DEG2RAD(0), 0, 0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi movement: swinging side to side without touching the floor with the
// heel
//--  Parameters:
//--     steps: Number of steps
//--     T : Period
//--     h : Amount of swing (from 0 to 50 aprox)
//---------------------------------------------------------
void Palqiqi::TiptoeSwing(float steps, int period, int height) {
  //-- Both feets are in phase. The offset is not half the amplitude in order to
  // tiptoe
  //-- It causes the robot to swing from side to side
  int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
  int O[SERVO_COUNT] = {
      0, 0, height, -height, HAND_HOME_POSITION, HAND_HOME_POSITION};
  double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi gait: Jitter
//--  Parameters:
//--    steps: Number of jitters
//--    T: Period of one jitter
//--    h: height (Values between 5 - 25)
//---------------------------------------------------------
void Palqiqi::Jitter(float steps, int period, int height) {
  //-- Both feet are 180 degrees out of phase
  //-- Feet amplitude and offset are the same
  //-- Initial phase for the right foot is -90, so that it starts
  //--   in one extreme position (not in the middle)
  //-- h is constrained to avoid hit the feets
  height = (height < 25) ? height : 25;
  int A[SERVO_COUNT] = {height, height, 0, 0, 0, 0};
  int O[SERVO_COUNT] = {
      0, 0, 0, 0, HAND_HOME_POSITION, HAND_HOME_POSITION};
  double phase_diff[SERVO_COUNT] = {DEG2RAD(-90), DEG2RAD(90), 0, 0, 0, 0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi gait: Ascending & turn (Jitter while up&down)
//--  Parameters:
//--    steps: Number of bends
//--    T: Period of one bend
//--    h: height (Values between 5 - 15)
//---------------------------------------------------------
void Palqiqi::AscendingTurn(float steps, int period, int height) {
  //-- Both feet and legs are 180 degrees out of phase
  //-- Initial phase for the right foot is -90, so that it starts
  //--   in one extreme position (not in the middle)
  //-- h is constrained to avoid hit the feets
  height = (height < 13) ? height : 13;
  int A[SERVO_COUNT] = {height, height, height, height, 0, 0};
  int O[SERVO_COUNT] = {0,
                        0,
                        height + 4,
                        -height + 4,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90};
  double phase_diff[SERVO_COUNT] = {DEG2RAD(-90), DEG2RAD(90), DEG2RAD(-90),
                                    DEG2RAD(90),  0,           0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}


//---------------------------------------------------------
//-- Palqiqi gait: Moonwalker. Palqiqi moves like Michael Jackson
//--  Parameters:
//--    Steps: Number of steps
//--    T: Period
//--    h: Height. Typical valures between 15 and 40
//--    dir: Direction: LEFT / RIGHT
//---------------------------------------------------------
void Palqiqi::Moonwalker(float steps, int period, int height, int dir) {
  //-- This motion is similar to that of the caterpillar robots: A travelling
  //-- wave moving from one side to another
  //-- The two Palqiqi's feet are equivalent to a minimal configuration. It is
  // known
  //-- that 2 servos can move like a worm if they are 120 degrees out of phase
  //-- In the example of Palqiqi, the two feet are mirrored so that we have:
  //--    180 - 120 = 60 degrees. The actual phase difference given to the
  // oscillators
  //--  is 60 degrees.
  //--  Both amplitudes are equal. The offset is half the amplitud plus a little
  // bit of
  //-   offset so that the robot tiptoe lightly

  int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
  int O[SERVO_COUNT] = {0,
                        0,
                        height / 2 + 2,
                        -height / 2 - 2,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90};
  int phi = -dir * 90;
  double phase_diff[SERVO_COUNT] = {
      0, 0, DEG2RAD(phi), DEG2RAD(-60 * dir + phi), 0, 0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//----------------------------------------------------------
//-- Palqiqi gait: Crusaito. A mixture between moonwalker and walk
//--   Parameters:
//--     steps: Number of steps
//--     T: Period
//--     h: height (Values between 20 - 50)
//--     dir:  Direction: LEFT / RIGHT
//-----------------------------------------------------------
void Palqiqi::Crusaito(float steps, int period, int height, int dir) {
  int A[SERVO_COUNT] = {25, 25, height, height, 0, 0};
  int O[SERVO_COUNT] = {0,
                        0,
                        height / 2 + 4,
                        -height / 2 - 4,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90};
  double phase_diff[SERVO_COUNT] = {90, 90, DEG2RAD(0), DEG2RAD(-60 * dir),
                                    0,  0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- Palqiqi gait: Flapping
//--  Parameters:
//--    steps: Number of steps
//--    T: Period
//--    h: height (Values between 10 - 30)
//--    dir: direction: FOREWARD, BACKWARD
//---------------------------------------------------------
void Palqiqi::Flapping(float steps, int period, int height, int dir) {
  int A[SERVO_COUNT] = {12, 12, height, height, 0, 0};
  int O[SERVO_COUNT] = {0,
                        0,
                        height - 10,
                        -height + 10,
                        HAND_HOME_POSITION - 90,
                        (HAND_HOME_POSITION) - 90};
  double phase_diff[SERVO_COUNT] = {
      DEG2RAD(0), DEG2RAD(180), DEG2RAD(-90 * dir), DEG2RAD(90 * dir), 0, 0};

  //-- Let's oscillate the servos!
  Execute(A, O, period, phase_diff, steps);
}

//---------------------------------------------------------
//-- 手部动作: 举手
//--  Parameters:
//--    period: 动作时间
//--    dir: 方向 1=左手, -1=右手, 0=双手
//--  注意：右手舵机安装方向相反，抬起=小角度，放下=大角度
//---------------------------------------------------------
void Palqiqi::HandsUp(int period, int dir) {
  if (!has_hands_) {
    return;
  }

  int initial[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};
  int target[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};

  // 左手抬起：45度（小角度=抬起）
  // 右手抬起：45度（修正：右手安装方向相反，小角度=抬起）
  if (dir == 0) {
    target[LEFT_HAND] = 45;   // 左手抬起
    target[RIGHT_HAND] = 45;  // 右手抬起（修正）
  } else if (dir == 1) {
    target[LEFT_HAND] = 45;
    target[RIGHT_HAND] = servo_[RIGHT_HAND].GetPosition();
  } else if (dir == -1) {
    target[RIGHT_HAND] = 45;  // 右手抬起（修正）
    target[LEFT_HAND] = servo_[LEFT_HAND].GetPosition();
  }

  MoveServos(period, target);
}

//---------------------------------------------------------
//-- 手部动作: 双手放下
//--  Parameters:
//--    period: 动作时间
//--    dir: 方向 1=左手, -1=右手, 0=双手
//--  注意：右手舵机安装方向相反，抬起=小角度，放下=大角度
//---------------------------------------------------------
void Palqiqi::HandsDown(int period, int dir) {
  if (!has_hands_) {
    return;
  }

  // 左手放下：HAND_HOME_POSITION (170度，大角度=放下)
  // 右手放下：HAND_HOME_POSITION (170度，修正：右手安装方向相反，大角度=放下)
  int target[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};

  if (dir == 1) {
    target[RIGHT_HAND] = servo_[RIGHT_HAND].GetPosition();
  } else if (dir == -1) {
    target[LEFT_HAND] = servo_[LEFT_HAND].GetPosition();
  }

  MoveServos(period, target);
}

//---------------------------------------------------------
//-- 手部动作: 挥手
//--  Parameters:
//--    period: 动作周期
//--    dir: 方向 LEFT/RIGHT/BOTH
//---------------------------------------------------------
void Palqiqi::HandWave(int period, int dir) {
  if (!has_hands_) {
    return;
  }

  if (dir == BOTH) {
    HandWaveBoth(period);
    return;
  }

  int servo_index = (dir == LEFT) ? LEFT_HAND : RIGHT_HAND;

  int current_positions[SERVO_COUNT];
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      current_positions[i] = servo_[i].GetPosition();
    } else {
      current_positions[i] = 90;
    }
  }

  int position;
  if (servo_index == LEFT_HAND) {
    position = 45; // 左手抬起
  } else {
    position = 135; // 右手抬起 (180-45)
  }

  current_positions[servo_index] = position;
  MoveServos(300, current_positions);
  vTaskDelay(pdMS_TO_TICKS(300));

  // 左右摆动5次
  for (int i = 0; i < 5; i++) {
    if (servo_index == LEFT_HAND) {
      current_positions[servo_index] = position - 30;
      MoveServos(period / 10, current_positions);
      vTaskDelay(pdMS_TO_TICKS(period / 10));
      current_positions[servo_index] = position + 30;
      MoveServos(period / 10, current_positions);
    } else {
      current_positions[servo_index] = position + 30;
      MoveServos(period / 10, current_positions);
      vTaskDelay(pdMS_TO_TICKS(period / 10));
      current_positions[servo_index] = position - 30;
      MoveServos(period / 10, current_positions);
    }
    vTaskDelay(pdMS_TO_TICKS(period / 10));
  }

  if (servo_index == LEFT_HAND) {
    current_positions[servo_index] = HAND_HOME_POSITION;
  } else {
    current_positions[servo_index] = HAND_HOME_POSITION;
  }
  MoveServos(300, current_positions);
}

//---------------------------------------------------------
//-- 手部动作: 双手同时挥手
//--  Parameters:
//--    period: 动作周期
//---------------------------------------------------------
void Palqiqi::HandWaveBoth(int period) {
  if (!has_hands_) {
    return;
  }

  int current_positions[SERVO_COUNT];
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      current_positions[i] = servo_[i].GetPosition();
    } else {
      current_positions[i] = 90;
    }
  }

  int left_position = 45;   // 左手抬起
  int right_position = 135; // 右手抬起 (180-45)

  current_positions[LEFT_HAND] = left_position;
  current_positions[RIGHT_HAND] = right_position;
  MoveServos(300, current_positions);

  // 左右摆动5次
  for (int i = 0; i < 5; i++) {
    // 波浪向左
    current_positions[LEFT_HAND] = left_position - 30;
    current_positions[RIGHT_HAND] = right_position + 30;
    MoveServos(period / 10, current_positions);

    // 波浪向右
    current_positions[LEFT_HAND] = left_position + 30;
    current_positions[RIGHT_HAND] = right_position - 30;
    MoveServos(period / 10, current_positions);
  }

  current_positions[LEFT_HAND] = HAND_HOME_POSITION;
  current_positions[RIGHT_HAND] = HAND_HOME_POSITION;
  MoveServos(300, current_positions);
}


///////////////////////////////////////////////////////////////////
//-- 贝塞尔轨迹运动函数 -----------------------------------------//
///////////////////////////////////////////////////////////////////

//---------------------------------------------------------
//-- 使用指定缓动类型移动舵机
//--  Parameters:
//--    time: 运动时间(ms)
//--    servo_target: 目标位置数组
//--    ease_type: 缓动类型
//---------------------------------------------------------
void Palqiqi::MoveServosWithEase(int time, int servo_target[], EaseType ease_type) {
  if (GetRestState() == true) {
    SetRestState(false);
  }

  if (time <= 10) {
    // 快速动作直接设置
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (servo_pins_[i] != -1) {
        servo_[i].SetPosition(servo_target[i]);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(time));
    return;
  }

  // 记录起始位置
  int start_positions[SERVO_COUNT];
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      start_positions[i] = servo_[i].GetPosition();
    }
  }

  unsigned long start_time = millis();
  unsigned long end_time = start_time + time;

  while (millis() < end_time) {
    unsigned long elapsed = millis() - start_time;
    float progress = (float)elapsed / (float)time;

    // 使用指定的缓动类型
    float ease_progress = GetEaseByType(progress, ease_type);

    // 更新所有舵机位置
    for (int i = 0; i < SERVO_COUNT; i++) {
      if (servo_pins_[i] != -1) {
        int new_pos = start_positions[i] +
                      (int)((servo_target[i] - start_positions[i]) * ease_progress);
        servo_[i].SetPosition(new_pos);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 确保到达目标位置
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].SetPosition(servo_target[i]);
    }
  }
}

//---------------------------------------------------------
//-- 单个舵机沿贝塞尔路径运动
//--  Parameters:
//--    servo_index: 舵机索引
//--    waypoints: 路径点数组
//--    count: 路径点数量
//---------------------------------------------------------
void Palqiqi::MoveServoPath(int servo_index, BezierWaypoint waypoints[], int count) {
  if (servo_index < 0 || servo_index >= SERVO_COUNT ||
      servo_pins_[servo_index] == -1 || count < 1) {
    return;
  }

  if (GetRestState() == true) {
    SetRestState(false);
  }

  int current_pos = servo_[servo_index].GetPosition();

  for (int i = 0; i < count; i++) {
    int target_pos = waypoints[i].position;
    int duration = waypoints[i].duration_ms;
    EaseType ease = waypoints[i].ease;

    if (duration <= 10) {
      servo_[servo_index].SetPosition(target_pos);
      vTaskDelay(pdMS_TO_TICKS(duration));
    } else {
      unsigned long start_time = millis();
      unsigned long end_time = start_time + duration;

      while (millis() < end_time) {
        unsigned long elapsed = millis() - start_time;
        float progress = (float)elapsed / (float)duration;
        float ease_progress = GetEaseByType(progress, ease);

        int new_pos = current_pos + (int)((target_pos - current_pos) * ease_progress);
        servo_[servo_index].SetPosition(new_pos);

        vTaskDelay(pdMS_TO_TICKS(10));
      }

      servo_[servo_index].SetPosition(target_pos);
    }

    current_pos = target_pos;
  }
}

//---------------------------------------------------------
//-- 平滑挥手动作（使用贝塞尔曲线）
//--  比原版更自然，有回弹效果
//---------------------------------------------------------
void Palqiqi::HandWaveSmooth(int period, int dir) {
  if (!has_hands_) {
    return;
  }

  int servo_index = (dir == LEFT) ? LEFT_HAND : RIGHT_HAND;
  int home_pos = (servo_index == LEFT_HAND) ? HAND_HOME_POSITION
                                            : (HAND_HOME_POSITION);
  int up_pos = (servo_index == LEFT_HAND) ? 45 : 135;
  int wave_amplitude = 25;

  // 定义挥手路径：抬起 -> 左摆 -> 右摆 -> 左摆 -> 右摆 -> 放下
  BezierWaypoint path[] = {
      {up_pos, 400, EASE_OUT_BACK},  // 抬起（带回弹）
      {up_pos - wave_amplitude, period / 8, EASE_IN_OUT},
      {up_pos + wave_amplitude, period / 4, EASE_IN_OUT},
      {up_pos - wave_amplitude, period / 4, EASE_IN_OUT},
      {up_pos + wave_amplitude, period / 4, EASE_IN_OUT},
      {up_pos, period / 8, EASE_IN_OUT},
      {home_pos, 400, EASE_IN_OUT},  // 放下
  };

  // 右手方向相反
  if (servo_index == RIGHT_HAND) {
    for (int i = 1; i < 6; i++) {
      int offset = path[i].position - up_pos;
      path[i].position = up_pos - offset;
    }
  }

  MoveServoPath(servo_index, path, 7);
}

//---------------------------------------------------------
//-- 弹跳跳跃（使用贝塞尔曲线）
//--  更有弹性的跳跃效果
//---------------------------------------------------------
void Palqiqi::JumpBounce(int period) {
  // 准备姿势（下蹲）
  int crouch[SERVO_COUNT] = {
      90, 90, 120, 60, HAND_HOME_POSITION, HAND_HOME_POSITION};
  MoveServosWithEase(period / 4, crouch, EASE_IN);

  // 跳起
  int up[SERVO_COUNT] = {
      90, 90, 150, 30, HAND_HOME_POSITION, HAND_HOME_POSITION};
  MoveServosWithEase(period / 4, up, EASE_OUT);

  // 落地（带弹跳）
  int down[SERVO_COUNT] = {
      LEFT_LEG_HOME, RIGHT_LEG_HOME, LEFT_FOOT_HOME, RIGHT_FOOT_HOME, HAND_HOME_POSITION, HAND_HOME_POSITION};
  MoveServosWithEase(period / 2, down, EASE_OUT_BOUNCE);
}

void Palqiqi::EnableServoLimit(int diff_limit) {
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].SetLimiter(diff_limit);
    }
  }
}

void Palqiqi::DisableServoLimit() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servo_pins_[i] != -1) {
      servo_[i].DisableLimiter();
    }
  }
}
