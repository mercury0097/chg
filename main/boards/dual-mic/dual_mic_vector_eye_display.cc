#include "dual_mic_vector_eye_display.h"

#include "application.h"
#include "config.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

#define TAG "DualMicVectorEyeDisplay"

const DualMicVectorEyeDisplay::EmotionNameMap
    DualMicVectorEyeDisplay::emotion_name_maps_[] = {
        {"neutral", vector_eyes::Emotion::Normal},
        {"relaxed", vector_eyes::Emotion::Sleepy},
        {"sleepy", vector_eyes::Emotion::Sleepy},
        {"happy", vector_eyes::Emotion::Happy},
        {"laughing", vector_eyes::Emotion::Glee},
        {"funny", vector_eyes::Emotion::Glee},
        {"loving", vector_eyes::Emotion::Happy},
        {"confident", vector_eyes::Emotion::Normal},
        {"winking", vector_eyes::Emotion::Happy},
        {"cool", vector_eyes::Emotion::Skeptic},
        {"delicious", vector_eyes::Emotion::Glee},
        {"kissy", vector_eyes::Emotion::Happy},
        {"silly", vector_eyes::Emotion::Glee},
        {"sad", vector_eyes::Emotion::Sad},
        {"crying", vector_eyes::Emotion::Sad},
        {"angry", vector_eyes::Emotion::Angry},
        {"furious", vector_eyes::Emotion::Furious},
        {"surprised", vector_eyes::Emotion::Surprised},
        {"shocked", vector_eyes::Emotion::Scared},
        {"thinking", vector_eyes::Emotion::Skeptic},
        {"confused", vector_eyes::Emotion::Worried},
        {"embarrassed", vector_eyes::Emotion::Unimpressed},
        {"focused", vector_eyes::Emotion::Focused},
        {"annoyed", vector_eyes::Emotion::Annoyed},
        {"suspicious", vector_eyes::Emotion::Suspicious},
        {"awe", vector_eyes::Emotion::Awe},
        {nullptr, vector_eyes::Emotion::Normal},
};

DualMicVectorEyeDisplay::DualMicVectorEyeDisplay(
    esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, int offset_x, int offset_y, bool mirror_x,
    bool mirror_y, bool swap_xy, std::function<void()> touch_callback)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy),
      touch_callback_(std::move(touch_callback)) {
  SetupCanvas();
  StartUpdateTimer();
}

DualMicVectorEyeDisplay::~DualMicVectorEyeDisplay() {
  StopUpdateTimer();
  if (face_ != nullptr) {
    delete face_;
    face_ = nullptr;
  }
  if (canvas_buf_ != nullptr) {
    lv_free(canvas_buf_);
    canvas_buf_ = nullptr;
  }
}

void DualMicVectorEyeDisplay::SetupCanvas() {
  DisplayLockGuard lock(static_cast<Display *>(this));

  if (emoji_label_ != nullptr) {
    lv_obj_del(emoji_label_);
    emoji_label_ = nullptr;
  }
  if (subtitle_label_ != nullptr) {
    lv_obj_del(subtitle_label_);
    subtitle_label_ = nullptr;
  }
  if (content_ != nullptr) {
    lv_obj_del(content_);
    content_ = nullptr;
  }

  content_ = lv_obj_create(container_);
  lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(content_, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
  lv_obj_set_style_border_width(content_, 0, 0);
  lv_obj_set_style_pad_all(content_, 0, 0);
  lv_obj_center(content_);

  const int canvas_size = (LV_HOR_RES > LV_VER_RES) ? (LV_VER_RES - 40) : LV_HOR_RES;
  const size_t buffer_size = canvas_size * canvas_size * sizeof(lv_color_t);

#if CONFIG_SPIRAM
  canvas_buf_ = static_cast<lv_color_t *>(
      heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM));
  if (canvas_buf_ == nullptr) {
    canvas_buf_ = static_cast<lv_color_t *>(lv_malloc(buffer_size));
  }
#else
  canvas_buf_ = static_cast<lv_color_t *>(lv_malloc(buffer_size));
#endif

  if (canvas_buf_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate vector eye canvas buffer");
    return;
  }

  canvas_ = lv_canvas_create(content_);
  lv_canvas_set_buffer(canvas_, canvas_buf_, canvas_size, canvas_size,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_center(canvas_);
  lv_canvas_fill_bg(canvas_, lv_color_black(), LV_OPA_COVER);

  face_ = new vector_eyes::VectorFace(canvas_size, canvas_size, 80);
  face_->SetCanvas(canvas_);
  face_->SetEyeColor(lv_color_hex(0x00D4AA));
  face_->SetBackgroundColor(lv_color_black());

  emoji_label_ = lv_label_create(content_);
  lv_label_set_text(emoji_label_, "");
  lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

  subtitle_label_ = lv_label_create(content_);
  lv_label_set_text(subtitle_label_, "");
  lv_obj_set_width(subtitle_label_, LV_HOR_RES * 0.95);
  lv_label_set_long_mode(subtitle_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(subtitle_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(subtitle_label_, lv_color_white(), 0);
  lv_obj_set_style_bg_color(subtitle_label_, lv_color_black(), 0);
  lv_obj_set_style_border_width(subtitle_label_, 0, 0);
  lv_obj_set_style_bg_opa(subtitle_label_, LV_OPA_70, 0);
  lv_obj_set_style_pad_ver(subtitle_label_, 3, 0);
  lv_obj_align(subtitle_label_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);

  touch_overlay_ = lv_obj_create(content_);
  lv_obj_set_size(touch_overlay_, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_style_bg_opa(touch_overlay_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(touch_overlay_, 0, 0);
  lv_obj_set_style_radius(touch_overlay_, 0, 0);
  lv_obj_set_style_pad_all(touch_overlay_, 0, 0);
  lv_obj_clear_flag(touch_overlay_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(touch_overlay_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(touch_overlay_, ScreenTouchEventHandler, LV_EVENT_CLICKED,
                      this);
  lv_obj_move_foreground(touch_overlay_);
}

void DualMicVectorEyeDisplay::StartUpdateTimer() {
  update_timer_ = lv_timer_create(UpdateTimerCallback, 50, this);
}

void DualMicVectorEyeDisplay::StopUpdateTimer() {
  if (update_timer_ != nullptr) {
    lv_timer_del(update_timer_);
    update_timer_ = nullptr;
  }
}

void DualMicVectorEyeDisplay::UpdateTimerCallback(lv_timer_t *timer) {
  auto *self =
      static_cast<DualMicVectorEyeDisplay *>(lv_timer_get_user_data(timer));
  if (self != nullptr) {
    self->OnUpdate();
  }
}

void DualMicVectorEyeDisplay::ScreenTouchEventHandler(lv_event_t *event) {
  auto *self = static_cast<DualMicVectorEyeDisplay *>(lv_event_get_user_data(event));
  if (self != nullptr) {
    self->HandleTouch();
  }
}

void DualMicVectorEyeDisplay::HandleTouch() {
  const uint32_t now = lv_tick_get();
  if (now - last_touch_tick_ < TOUCH_DEBOUNCE_MS) {
    return;
  }

  last_touch_tick_ = now;
  if (touch_callback_) {
    touch_callback_();
  }
}

void DualMicVectorEyeDisplay::OnUpdate() {
  if (face_ == nullptr || canvas_ == nullptr) {
    return;
  }

  DisplayLockGuard lock(static_cast<Display *>(this));
  CheckDemoMode();
  if (!demo_mode_) {
    CheckRandomEmotion();
  }

  face_->Update();
  face_->Draw();
  lv_obj_invalidate(canvas_);
}

void DualMicVectorEyeDisplay::CheckRandomEmotion() {
  if (!idle_mode_) {
    return;
  }

  auto &app = Application::GetInstance();
  if (app.GetDeviceState() != kDeviceStateIdle) {
    return;
  }

  const uint32_t now = lv_tick_get();
  if (next_emotion_interval_ == 0) {
    ScheduleNextEmotionChange();
    last_emotion_change_ = now;
    return;
  }

  if (now - last_emotion_change_ > next_emotion_interval_) {
    static const vector_eyes::Emotion idle_emotions[] = {
        vector_eyes::Emotion::Normal,      vector_eyes::Emotion::Normal,
        vector_eyes::Emotion::Sleepy,      vector_eyes::Emotion::Skeptic,
        vector_eyes::Emotion::Suspicious,  vector_eyes::Emotion::Focused,
    };

    const int index = rand() % (sizeof(idle_emotions) / sizeof(idle_emotions[0]));
    const auto new_emotion = idle_emotions[index];
    if (new_emotion != current_emotion_) {
      current_emotion_ = new_emotion;
      face_->SetExpression(new_emotion);
    }

    last_emotion_change_ = now;
    ScheduleNextEmotionChange();
  }
}

void DualMicVectorEyeDisplay::CheckDemoMode() {
  if (!demo_mode_) {
    return;
  }

  const uint32_t now = lv_tick_get();
  if (demo_start_time_ == 0) {
    demo_start_time_ = now;
    demo_emotion_index_ = 0;
  }

  if (now - demo_start_time_ <= 2000) {
    return;
  }

  static const vector_eyes::Emotion all_emotions[] = {
      vector_eyes::Emotion::Normal,      vector_eyes::Emotion::Happy,
      vector_eyes::Emotion::Glee,        vector_eyes::Emotion::Sad,
      vector_eyes::Emotion::Worried,     vector_eyes::Emotion::Focused,
      vector_eyes::Emotion::Annoyed,     vector_eyes::Emotion::Surprised,
      vector_eyes::Emotion::Skeptic,     vector_eyes::Emotion::Frustrated,
      vector_eyes::Emotion::Unimpressed, vector_eyes::Emotion::Sleepy,
      vector_eyes::Emotion::Suspicious,  vector_eyes::Emotion::Squint,
      vector_eyes::Emotion::Angry,       vector_eyes::Emotion::Furious,
      vector_eyes::Emotion::Scared,      vector_eyes::Emotion::Awe,
  };

  if (demo_emotion_index_ < static_cast<int>(sizeof(all_emotions) / sizeof(all_emotions[0]))) {
    face_->SetExpression(all_emotions[demo_emotion_index_++]);
    demo_start_time_ = now;
    return;
  }

  demo_mode_ = false;
  idle_mode_ = true;
  face_->SetExpression(vector_eyes::Emotion::Normal);
}

void DualMicVectorEyeDisplay::ScheduleNextEmotionChange() {
  next_emotion_interval_ = 8000 + (rand() % 7000);
}

vector_eyes::Emotion
DualMicVectorEyeDisplay::MapEmotionName(const char *name) {
  if (name == nullptr) {
    return vector_eyes::Emotion::Normal;
  }

  for (const auto &map : emotion_name_maps_) {
    if (map.name != nullptr && strcmp(map.name, name) == 0) {
      return map.emotion;
    }
  }

  return vector_eyes::Emotion::Normal;
}

void DualMicVectorEyeDisplay::SetEmotion(const char *emotion) {
  if (emotion == nullptr || face_ == nullptr) {
    return;
  }

  DisplayLockGuard lock(static_cast<Display *>(this));
  const auto mapped = MapEmotionName(emotion);
  if (mapped == vector_eyes::Emotion::Normal) {
    idle_mode_ = true;
  } else {
    idle_mode_ = false;
    current_emotion_ = mapped;
  }
  face_->SetExpression(mapped);
}

void DualMicVectorEyeDisplay::SetChatMessage(const char *role, const char *content) {
  DisplayLockGuard lock(static_cast<Display *>(this));
  if (subtitle_label_ == nullptr) {
    return;
  }

  if (!subtitles_visible_ || content == nullptr || strlen(content) == 0) {
    lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_label_set_text(subtitle_label_, content);
  lv_obj_remove_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
  ESP_LOGI(TAG, "Set chat message [%s]: %s", role, content);
}

void DualMicVectorEyeDisplay::SetTheme(Theme *theme) {
  DisplayLockGuard lock(static_cast<Display *>(this));
  Display::SetTheme(theme);
}
