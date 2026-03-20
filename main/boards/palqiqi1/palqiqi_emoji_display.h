#pragma once

#include <libs/gif/lv_gif.h>
#include <string.h>

#include "display/lcd_display.h"
#include "otto_emoji_gif.h"

/**
 * @brief Palqiqi机器人GIF表情显示类
 * 继承LcdDisplay，添加GIF表情支持
 */
class PalqiqiEmojiDisplay : public SpiLcdDisplay {
public:
    /**
     * @brief 构造函数，参数与SpiLcdDisplay相同
     */
    PalqiqiEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                        int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                        bool swap_xy);

    virtual ~PalqiqiEmojiDisplay() = default;

    // 重写表情设置方法
    virtual void SetEmotion(const char* emotion) override;

    // 重写聊天消息设置方法
    virtual void SetChatMessage(const char* role, const char* content) override;

private:
    void SetupGifContainer();
    void EnsureTearObjectsCreated();
    void StartTears();
    void StopTears();
    void AnimateTear(lv_obj_t* tear, int x, int start_y, int end_y, int duration_ms, int delay_ms);

    lv_obj_t* emotion_gif_;  ///< GIF表情组件
    lv_obj_t* tear_left_ = nullptr;   ///< 左侧泪滴覆盖层
    lv_obj_t* tear_right_ = nullptr;  ///< 右侧泪滴覆盖层
    bool tears_active_ = false;

    // 表情映射
    struct EmotionMap {
        const char* name;
        const lv_img_dsc_t* gif;
    };

    static const EmotionMap emotion_maps_[];
};
