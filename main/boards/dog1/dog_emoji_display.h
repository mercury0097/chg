/**
 * @file dog_emoji_display.h
 * @brief 桌面小狗机器人GIF表情显示类
 */

#pragma once

#include "display/lcd_display.h"

/**
 * @brief 桌面小狗机器人GIF表情显示类
 */
class DogEmojiDisplay : public SpiLcdDisplay {
public:
    DogEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, 
                    esp_lcd_panel_handle_t panel, 
                    int width, int height, 
                    int offset_x, int offset_y, 
                    bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                        mirror_x, mirror_y, swap_xy) {}
    
    virtual ~DogEmojiDisplay() {}
};









