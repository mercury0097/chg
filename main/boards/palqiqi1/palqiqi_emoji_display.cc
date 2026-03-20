#include "palqiqi_emoji_display.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <font_awesome.h>

#include <algorithm>
#include <cstring>
#include <string.h>
#include <string>

#include "display/lcd_display.h"

#define TAG "PalqiqiEmojiDisplay"

// 表情映射表 - 将原版21种表情映射到现有6个GIF
const PalqiqiEmojiDisplay::EmotionMap PalqiqiEmojiDisplay::emotion_maps_[] = {
    // 中性/平静类表情 -> staticstate
    {"neutral", &staticstate},
    {"relaxed", &staticstate},
    {"sleepy", &staticstate},

    // 积极/开心类表情 -> happy
    {"happy", &happy},
    {"laughing", &happy},
    {"funny", &happy},
    {"loving", &happy},
    {"confident", &happy},
    {"winking", &happy},
    {"cool", &happy},
    {"delicious", &happy},
    {"kissy", &happy},
    {"silly", &happy},

    // 悲伤类表情 -> sad
    {"sad", &sad},
    {"crying", &sad},

    // 愤怒类表情 -> anger
    {"angry", &anger},

    // 惊讶类表情 -> scare
    {"surprised", &scare},
    {"shocked", &scare},

    // 思考/困惑类表情 -> buxue
    {"thinking", &buxue},
    {"confused", &buxue},
    {"embarrassed", &buxue},

    {nullptr, nullptr} // 结束标记
};


PalqiqiEmojiDisplay::PalqiqiEmojiDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel, int width,
                                   int height, int offset_x, int offset_y,
                                   bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy),
      emotion_gif_(nullptr) {
  SetupGifContainer();
};

static void TearAnimExec(void *obj, int32_t v) {
  lv_obj_set_y(static_cast<lv_obj_t *>(obj), v);
}

void PalqiqiEmojiDisplay::SetupGifContainer() {
  DisplayLockGuard lock(static_cast<Display *>(this));

  if (emoji_label_) {
    lv_obj_del(emoji_label_);
  }

  if (chat_message_label_) {
    lv_obj_del(chat_message_label_);
  }
  if (content_) {
    lv_obj_del(content_);
  }

  content_ = lv_obj_create(container_);
  lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(content_, LV_HOR_RES, LV_HOR_RES);
  // 🎨 设置背景为纯黑色，确保只有眼睛是黄色
  lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
  lv_obj_set_style_border_width(content_, 0, 0);
  lv_obj_set_flex_grow(content_, 1);
  lv_obj_center(content_);

  emoji_label_ = lv_label_create(content_);
  lv_label_set_text(emoji_label_, "");
  lv_obj_set_width(emoji_label_, 0);
  lv_obj_set_style_border_width(emoji_label_, 0, 0);
  lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

  // 创建 GIF 表情并设置黄色重新着色
  emotion_gif_ = lv_gif_create(content_);
  int gif_size = LV_HOR_RES;
  lv_obj_set_size(emotion_gif_, gif_size, gif_size);
  lv_obj_set_style_border_width(emotion_gif_, 0, 0);
  lv_obj_set_style_bg_opa(emotion_gif_, LV_OPA_TRANSP, 0);

  lv_obj_center(emotion_gif_);
  lv_gif_set_src(emotion_gif_, &staticstate);

  // 注意：LVGL 的 img_recolor 会给整个图像着色（包括背景）
  // 暂时禁用重新着色，保持经典黑白风格：黑色背景 + 白色眼睛
  // 如果需要彩色眼睛，需要替换 GIF 文件本身
  // lv_obj_set_style_img_recolor_opa(emotion_gif_, LV_OPA_60, 0);
  // lv_obj_set_style_img_recolor(emotion_gif_, lv_color_hex(0xFFD700), 0);

  // 确保泪滴覆盖层对象已创建（默认隐藏）
  EnsureTearObjectsCreated();

  chat_message_label_ = lv_label_create(content_);
  lv_label_set_text(chat_message_label_, "");
  lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
  lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);

  // 使用当前主题的文字颜色和背景色
  if (current_theme_ != nullptr) {
    LvglTheme *lvgl_theme = dynamic_cast<LvglTheme *>(current_theme_);
    if (lvgl_theme != nullptr) {
      lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(),
                                  0);
      lv_obj_set_style_bg_color(chat_message_label_,
                                lvgl_theme->chat_background_color(), 0);
    } else {
      // 如果转换失败，使用默认颜色
      lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
      lv_obj_set_style_bg_color(chat_message_label_, lv_color_black(), 0);
    }
  } else {
    // 如果没有主题，使用默认颜色
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(chat_message_label_, lv_color_black(), 0);
  }

  lv_obj_set_style_border_width(chat_message_label_, 0, 0);
  lv_obj_set_style_bg_opa(chat_message_label_, LV_OPA_70, 0);
  lv_obj_set_style_pad_ver(chat_message_label_, 5, 0);
  lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, 0);

  // 默认使用深色主题
  auto &theme_manager = LvglThemeManager::GetInstance();
  auto dark_theme = theme_manager.GetTheme("dark");
  if (dark_theme != nullptr) {
    LcdDisplay::SetTheme(dark_theme);
  }
}


void PalqiqiEmojiDisplay::SetEmotion(const char *emotion) {
  if (!emotion || !emotion_gif_) {
    return;
  }

  DisplayLockGuard lock(static_cast<Display *>(this));

  for (const auto &map : emotion_maps_) {
    if (map.name && strcmp(map.name, emotion) == 0) {
      lv_gif_set_src(emotion_gif_, map.gif);
      // sad/crying 显示流泪，其它表情关闭
      if (strcmp(emotion, "sad") == 0 || strcmp(emotion, "crying") == 0) {
        StartTears();
      } else {
        StopTears();
      }
      ESP_LOGI(TAG, "设置表情: %s", emotion);
      return;
    }
  }

  lv_gif_set_src(emotion_gif_, &staticstate);
  StopTears();
  ESP_LOGI(TAG, "未知表情'%s'，使用默认", emotion);
}

void PalqiqiEmojiDisplay::SetChatMessage(const char *role, const char *content) {
  DisplayLockGuard lock(static_cast<Display *>(this));
  if (chat_message_label_ == nullptr) {
    return;
  }

  if (content == nullptr || strlen(content) == 0) {
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_label_set_text(chat_message_label_, content);
  lv_obj_remove_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

  ESP_LOGI(TAG, "设置聊天消息 [%s]: %s", role, content);
}

void PalqiqiEmojiDisplay::EnsureTearObjectsCreated() {
  if (tear_left_ && tear_right_) {
    return;
  }

  // 左右泪滴为圆角矩形，亮青色，默认隐藏
  tear_left_ = lv_obj_create(content_);
  tear_right_ = lv_obj_create(content_);

  lv_obj_set_size(tear_left_, 10, 16);
  lv_obj_set_size(tear_right_, 10, 16);
  lv_obj_set_style_radius(tear_left_, 6, 0);
  lv_obj_set_style_radius(tear_right_, 6, 0);
  lv_obj_set_style_bg_opa(tear_left_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(tear_right_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tear_left_, lv_color_hex(0x00FFFF), 0);
  lv_obj_set_style_bg_color(tear_right_, lv_color_hex(0x00FFFF), 0);
  lv_obj_set_style_border_width(tear_left_, 0, 0);
  lv_obj_set_style_border_width(tear_right_, 0, 0);
  lv_obj_add_flag(tear_left_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tear_right_, LV_OBJ_FLAG_HIDDEN);

  // 初始位置（基于 240x240），后续动画时会动态设置 Y
  lv_obj_set_x(tear_left_, LV_HOR_RES * 35 / 100);
  lv_obj_set_x(tear_right_, LV_HOR_RES * 65 / 100);
}

void PalqiqiEmojiDisplay::AnimateTear(lv_obj_t *tear, int x, int start_y,
                                   int end_y, int duration_ms, int delay_ms) {
  lv_obj_set_x(tear, x);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, tear);
  lv_anim_set_values(&a, start_y, end_y);
  lv_anim_set_time(&a, duration_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_early_apply(&a, true);
  lv_anim_set_delay(&a, delay_ms);
  lv_anim_set_exec_cb(&a, TearAnimExec);
  lv_anim_start(&a);
}

void PalqiqiEmojiDisplay::StartTears() {
  if (tears_active_) {
    return;
  }
  EnsureTearObjectsCreated();
  tears_active_ = true;

  lv_obj_clear_flag(tear_left_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(tear_right_, LV_OBJ_FLAG_HIDDEN);

  const int top_y = LV_HOR_RES * 55 / 100; // 眼睛下方
  const int bottom_y = LV_HOR_RES - 10;    // 底部留边
  const int left_x = LV_HOR_RES * 35 / 100;
  const int right_x = LV_HOR_RES * 65 / 100;

  // 左右错峰，形成自然流动
  AnimateTear(tear_left_, left_x, top_y, bottom_y, 900, 0);
  AnimateTear(tear_right_, right_x, top_y, bottom_y, 1000, 200);
}

void PalqiqiEmojiDisplay::StopTears() {
  if (!tears_active_) {
    return;
  }
  tears_active_ = false;

  if (tear_left_) {
    lv_obj_add_flag(tear_left_, LV_OBJ_FLAG_HIDDEN);
  }
  if (tear_right_) {
    lv_obj_add_flag(tear_right_, LV_OBJ_FLAG_HIDDEN);
  }
}
