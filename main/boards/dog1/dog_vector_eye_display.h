/**
 * @file dog_vector_eye_display.h
 * @brief 桌面小狗机器人矢量眼睛显示类
 * 
 * 使用矢量绘制的眼睛替代 GIF 表情
 */

#pragma once

#include <lvgl.h>
#include <string.h>

#include "display/lcd_display.h"
#include "vector_eyes/vector_face.h"
#include "vector_eyes/emotions.h"

/**
 * @brief 桌面小狗机器人矢量眼睛显示类
 * 继承SpiLcdDisplay，使用矢量绘制眼睛
 */
class DogVectorEyeDisplay : public SpiLcdDisplay {
public:
    /**
     * @brief 构造函数
     */
    DogVectorEyeDisplay(esp_lcd_panel_io_handle_t panel_io, 
                        esp_lcd_panel_handle_t panel, 
                        int width, int height, 
                        int offset_x, int offset_y, 
                        bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~DogVectorEyeDisplay();

    // 重写表情设置方法
    virtual void SetEmotion(const char* emotion) override;

    // 重写聊天消息设置方法
    virtual void SetChatMessage(const char* role, const char* content) override;

    // 重写主题设置方法 - 矢量眼睛使用固定风格
    virtual void SetTheme(Theme* theme) override;

    /**
     * @brief 手动触发眨眼
     */
    void Blink();

    /**
     * @brief 看向指定方向
     */
    void LookAt(float x, float y);

    /**
     * @brief 设置眼睛颜色
     */
    void SetEyeColor(uint32_t color_hex);

private:
    void SetupCanvas();
    void StartUpdateTimer();
    void StopUpdateTimer();
    
    static void UpdateTimerCallback(lv_timer_t* timer);
    void OnUpdate();

    // 表情名称到枚举的映射
    vector_eyes::Emotion MapEmotionName(const char* name);

    lv_obj_t* canvas_ = nullptr;
    lv_color_t* canvas_buf_ = nullptr;
    vector_eyes::VectorFace* face_ = nullptr;
    lv_timer_t* update_timer_ = nullptr;

    lv_obj_t* chat_message_label_ = nullptr;

    // 随机表情变化
    uint32_t last_emotion_change_ = 0;
    uint32_t next_emotion_interval_ = 0;
    vector_eyes::Emotion current_emotion_ = vector_eyes::Emotion::Normal;
    bool idle_mode_ = true;  // 空闲模式下才随机变化
    
    // 表情演示模式
    bool demo_mode_ = false;   // 禁用演示模式
    int demo_emotion_index_ = 0;
    uint32_t demo_start_time_ = 0;
    
    void CheckRandomEmotion();
    void CheckDemoMode();
    void ScheduleNextEmotionChange();

    // 表情名称映射表
    struct EmotionNameMap {
        const char* name;
        vector_eyes::Emotion emotion;
    };
    static const EmotionNameMap emotion_name_maps_[];
};
