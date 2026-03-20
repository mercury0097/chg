#ifndef PALQIQI_API_ADAPTER_H
#define PALQIQI_API_ADAPTER_H

#include "robot_api_server.h"
#include "board.h"
#include "settings.h"
#include "config.h"
#include <esp_log.h>

// 从 palqiqi_controller.cc 导入的函数声明
extern bool PalqiqiControllerIsIdle();
extern std::string PalqiqiControllerGetCurrentAction();
extern bool PalqiqiControllerExecuteAction(const std::string& action, int steps, int speed);
extern bool PalqiqiControllerHasHands();

/**
 * @brief Palqiqi机器人的API适配器
 * 
 * 实现IRobotActions接口，将HTTP API请求转换为Palqiqi控制器的调用
 */
class PalqiqiApiAdapter : public IRobotActions {
public:
    PalqiqiApiAdapter() {
        // 从NVS加载配置
        Settings settings("robot_api", false);
        device_id_ = settings.GetString("device_id", "palqiqi-002");
        
        ESP_LOGI("PalqiqiApiAdapter", "初始化完成 - 设备ID: %s", device_id_.c_str());
    }

    // ==================== 设备信息 ====================

    std::string GetDeviceId() override {
        return device_id_;
    }

    std::string GetModel() override {
        return "palqiqi";
    }

    std::vector<std::string> GetCapabilities() override {
        std::vector<std::string> caps = {
            "walk_forward",
            "walk_backward",
            "turn_left",
            "turn_right",
            "home",
            "stop",
            "jump",
            "swing",
            "moonwalk",
            "bend",
            "shake_leg",
            "updown",
            "look_around"
        };
        
        // 如果有手部舵机，添加手部动作
        if (PalqiqiControllerHasHands()) {
            caps.push_back("hands_up");
            caps.push_back("hands_down");
            caps.push_back("hand_wave");
        }
        
        // 添加通用能力
        caps.push_back("battery");
        caps.push_back("status");
        caps.push_back("volume");
        
        return caps;
    }

    std::string GetFirmwareVersion() override {
        return PALQIQI_VERSION;
    }

    // ==================== 动作控制 ====================

    bool ExecuteAction(const std::string& action, int steps, int speed) override {
        return PalqiqiControllerExecuteAction(action, steps, speed);
    }

    // ==================== 状态查询 ====================

    bool IsIdle() override {
        return PalqiqiControllerIsIdle();
    }

    std::string GetCurrentAction() override {
        return PalqiqiControllerGetCurrentAction();
    }

    int GetBatteryLevel() override {
        auto& board = Board::GetInstance();
        int level = 0;
        bool charging = false;
        bool discharging = false;
        board.GetBatteryLevel(level, charging, discharging);
        return level;
    }

    // ==================== 音量控制 ====================

    int GetVolume() override {
        auto& board = Board::GetInstance();
        auto* codec = board.GetAudioCodec();
        if (codec) {
            return codec->output_volume();
        }
        return 100; // 默认音量
    }

    bool SetVolume(int volume) override {
        // 限制音量范围 0-100
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        
        auto& board = Board::GetInstance();
        auto* codec = board.GetAudioCodec();
        if (codec) {
            codec->SetOutputVolume(volume);
            ESP_LOGI("PalqiqiApiAdapter", "音量设置为: %d", volume);
            return true;
        }
        ESP_LOGE("PalqiqiApiAdapter", "无法获取音频编解码器");
        return false;
    }

private:
    std::string device_id_;
};

#endif // PALQIQI_API_ADAPTER_H
