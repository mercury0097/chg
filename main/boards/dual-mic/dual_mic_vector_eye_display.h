#pragma once

#include <functional>
#include <lvgl.h>
#include <string.h>

#include "display/lcd_display.h"
#include "../dog1/vector_eyes/emotions.h"
#include "../dog1/vector_eyes/vector_face.h"

class DualMicVectorEyeDisplay : public SpiLcdDisplay {
public:
    DualMicVectorEyeDisplay(esp_lcd_panel_io_handle_t panel_io,
                            esp_lcd_panel_handle_t panel,
                            int width, int height,
                            int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy,
                            std::function<void()> touch_callback);

    virtual ~DualMicVectorEyeDisplay();

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetTheme(Theme* theme) override;

private:
    struct EmotionNameMap {
        const char* name;
        vector_eyes::Emotion emotion;
    };

    void SetupCanvas();
    void StartUpdateTimer();
    void StopUpdateTimer();
    void CheckRandomEmotion();
    void CheckDemoMode();
    void ScheduleNextEmotionChange();
    vector_eyes::Emotion MapEmotionName(const char* name);
    void OnUpdate();
    void HandleTouch();

    static void UpdateTimerCallback(lv_timer_t* timer);
    static void ScreenTouchEventHandler(lv_event_t* event);

    lv_obj_t* canvas_ = nullptr;
    lv_color_t* canvas_buf_ = nullptr;
    vector_eyes::VectorFace* face_ = nullptr;
    lv_timer_t* update_timer_ = nullptr;
    lv_obj_t* subtitle_label_ = nullptr;
    lv_obj_t* touch_overlay_ = nullptr;

    std::function<void()> touch_callback_;
    uint32_t last_touch_tick_ = 0;
    uint32_t last_emotion_change_ = 0;
    uint32_t next_emotion_interval_ = 0;
    uint32_t demo_start_time_ = 0;
    int demo_emotion_index_ = 0;
    vector_eyes::Emotion current_emotion_ = vector_eyes::Emotion::Normal;
    bool idle_mode_ = true;
    bool demo_mode_ = false;

    static const EmotionNameMap emotion_name_maps_[];
};
