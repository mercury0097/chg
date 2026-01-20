#ifndef ROBOT_API_SERVER_H
#define ROBOT_API_SERVER_H

#include <esp_http_server.h>
#include <string>
#include <vector>
#include <functional>

/**
 * @brief 机器人动作接口 - 每个机器人板子需要实现此接口
 * 
 * 通过实现此接口，可以让不同型号的机器人使用统一的HTTP API服务
 */
class IRobotActions {
public:
    virtual ~IRobotActions() = default;

    // ==================== 设备信息 ====================
    
    /**
     * @brief 获取设备ID
     * @return 设备ID字符串，如 "dog-001"
     */
    virtual std::string GetDeviceId() = 0;

    /**
     * @brief 获取设备型号
     * @return 型号字符串，如 "dog", "palqiqi"
     */
    virtual std::string GetModel() = 0;

    /**
     * @brief 获取设备能力列表
     * @return 支持的动作和功能列表，如 ["walk_forward", "turn_left", "battery", "status"]
     */
    virtual std::vector<std::string> GetCapabilities() = 0;

    /**
     * @brief 获取固件版本
     * @return 版本字符串，如 "1.0.0"
     */
    virtual std::string GetFirmwareVersion() = 0;

    // ==================== 动作控制 ====================

    /**
     * @brief 执行指定动作
     * @param action 动作名称
     * @param steps 步数/次数
     * @param speed 速度（毫秒）
     * @return 是否成功接收动作指令
     */
    virtual bool ExecuteAction(const std::string& action, int steps, int speed) = 0;

    // ==================== 状态查询 ====================

    /**
     * @brief 检查机器人是否空闲
     * @return true表示空闲，false表示正在执行动作
     */
    virtual bool IsIdle() = 0;

    /**
     * @brief 获取当前正在执行的动作名称
     * @return 动作名称，空闲时返回空字符串
     */
    virtual std::string GetCurrentAction() = 0;

    /**
     * @brief 获取电池电量
     * @return 电量百分比 0-100
     */
    virtual int GetBatteryLevel() = 0;

    // ==================== 音量控制 ====================

    /**
     * @brief 获取当前音量
     * @return 音量百分比 0-100
     */
    virtual int GetVolume() = 0;

    /**
     * @brief 设置音量
     * @param volume 音量百分比 0-100
     * @return 是否设置成功
     */
    virtual bool SetVolume(int volume) = 0;

};

/**
 * @brief HTTP API服务器 - 提供小程序控制接口
 * 
 * 实现以下API端点：
 * - GET  /api/v1/device/info   - 获取设备信息
 * - POST /api/v1/device/action - 发送动作指令
 * - GET  /api/v1/device/status - 获取设备状态
 * - POST /api/v1/device/volume - 设置音量
 * - GET  /api/v1/device/volume - 获取音量
 */
class RobotApiServer {
public:
    /**
     * @brief 获取单例实例
     */
    static RobotApiServer& GetInstance();

    /**
     * @brief 启动API服务器
     * @param robot 机器人动作接口实现
     * @param port HTTP服务端口，默认80
     */
    void Start(IRobotActions* robot, uint16_t port = 80);

    /**
     * @brief 停止API服务器
     */
    void Stop();

    /**
     * @brief 检查服务器是否正在运行
     */
    bool IsRunning() const { return server_ != nullptr; }

private:
    RobotApiServer() = default;
    ~RobotApiServer();
    RobotApiServer(const RobotApiServer&) = delete;
    RobotApiServer& operator=(const RobotApiServer&) = delete;

    // HTTP请求处理器
    static esp_err_t HandleDeviceInfo(httpd_req_t* req);
    static esp_err_t HandleDeviceAction(httpd_req_t* req);
    static esp_err_t HandleDeviceStatus(httpd_req_t* req);
    static esp_err_t HandleDeviceVolumeGet(httpd_req_t* req);
    static esp_err_t HandleDeviceVolumeSet(httpd_req_t* req);

    // 辅助方法
    static std::string GenerateActionId();
    static void SendJsonResponse(httpd_req_t* req, int code, const std::string& message, 
                                  const std::string& data_json = "{}");
    httpd_handle_t server_ = nullptr;
    IRobotActions* robot_ = nullptr;
};

#endif // ROBOT_API_SERVER_H
