#ifndef DOG_API_ADAPTER_H
#define DOG_API_ADAPTER_H

#include "robot_api_server.h"
#include "board.h"
#include "settings.h"
#include "config.h"
#include <esp_log.h>

// 从 dog_controller.cc 导入的函数声明
extern bool DogControllerIsIdle();
extern std::string DogControllerGetCurrentAction();
extern bool DogControllerExecuteAction(const std::string& action, int steps, int speed);

/**
 * @brief Dog机器人的API适配器
 * 
 * 实现IRobotActions接口，将HTTP API请求转换为Dog控制器的调用
 */
class DogApiAdapter : public IRobotActions {
public:
    DogApiAdapter() {
        // 从NVS加载配置
        Settings settings("robot_api", false);
        device_id_ = settings.GetString("device_id", "dog-001");
        
        ESP_LOGI("DogApiAdapter", "初始化完成 - 设备ID: %s", device_id_.c_str());
    }

    // ==================== 设备信息 ====================

    std::string GetDeviceId() override {
        return device_id_;
    }

    std::string GetModel() override {
        return "dog";
    }

    std::vector<std::string> GetCapabilities() override {
        return {
            "walk_forward",
            "walk_backward",
            "turn_left",
            "turn_right",
            "home",
            "stop",
            "say_hello",
            "sway_back_forth",
            "push_up",
            "sleep",
            "battery",
            "status",
            "volume"
        };
    }

    std::string GetFirmwareVersion() override {
        return DOG_VERSION;
    }

    // ==================== 动作控制 ====================

    bool ExecuteAction(const std::string& action, int steps, int speed) override {
        return DogControllerExecuteAction(action, steps, speed);
    }

    // ==================== 状态查询 ====================

    bool IsIdle() override {
        return DogControllerIsIdle();
    }

    std::string GetCurrentAction() override {
        return DogControllerGetCurrentAction();
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
            ESP_LOGI("DogApiAdapter", "音量设置为: %d", volume);
            return true;
        }
        ESP_LOGE("DogApiAdapter", "无法获取音频编解码器");
        return false;
    }

private:
    std::string device_id_;
};

#endif // DOG_API_ADAPTER_H
