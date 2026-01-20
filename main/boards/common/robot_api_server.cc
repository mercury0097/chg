#include "robot_api_server.h"
#include <esp_log.h>
#include <esp_random.h>
#include <cJSON.h>
#include <cstring>

#define TAG "RobotApiServer"

// ==================== 单例实现 ====================

RobotApiServer& RobotApiServer::GetInstance() {
    static RobotApiServer instance;
    return instance;
}

RobotApiServer::~RobotApiServer() {
    Stop();
}

// ==================== 服务器启动/停止 ====================

void RobotApiServer::Start(IRobotActions* robot, uint16_t port) {
    if (server_ != nullptr) {
        ESP_LOGW(TAG, "API服务器已在运行");
        return;
    }

    robot_ = robot;
    if (robot_ == nullptr) {
        ESP_LOGE(TAG, "机器人接口为空，无法启动API服务器");
        return;
    }

    // 配置HTTP服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "启动API服务器，端口: %d", port);

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "启动HTTP服务器失败");
        return;
    }

    // 注册API端点
    
    // GET /api/v1/device/info - 获取设备信息
    httpd_uri_t device_info = {
        .uri = "/api/v1/device/info",
        .method = HTTP_GET,
        .handler = HandleDeviceInfo,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &device_info);

    // POST /api/v1/device/action - 发送动作指令
    httpd_uri_t device_action = {
        .uri = "/api/v1/device/action",
        .method = HTTP_POST,
        .handler = HandleDeviceAction,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &device_action);

    // GET /api/v1/device/status - 获取设备状态
    httpd_uri_t device_status = {
        .uri = "/api/v1/device/status",
        .method = HTTP_GET,
        .handler = HandleDeviceStatus,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &device_status);

    // GET /api/v1/device/volume - 获取音量
    httpd_uri_t device_volume_get = {
        .uri = "/api/v1/device/volume",
        .method = HTTP_GET,
        .handler = HandleDeviceVolumeGet,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &device_volume_get);

    // POST /api/v1/device/volume - 设置音量
    httpd_uri_t device_volume_set = {
        .uri = "/api/v1/device/volume",
        .method = HTTP_POST,
        .handler = HandleDeviceVolumeSet,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &device_volume_set);

    ESP_LOGI(TAG, "API服务器启动成功，设备ID: %s, 型号: %s", 
             robot_->GetDeviceId().c_str(), robot_->GetModel().c_str());
}

void RobotApiServer::Stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGI(TAG, "API服务器已停止");
    }
}

// ==================== 辅助方法 ====================

std::string RobotApiServer::GenerateActionId() {
    // 生成6位随机短ID
    const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char id[7];
    for (int i = 0; i < 6; i++) {
        id[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    id[6] = '\0';
    return std::string(id);
}

void RobotApiServer::SendJsonResponse(httpd_req_t* req, int code, 
                                       const std::string& message, 
                                       const std::string& data_json) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "message", message.c_str());
    
    // 解析data_json并添加到响应
    cJSON* data = cJSON_Parse(data_json.c_str());
    if (data == nullptr) {
        data = cJSON_CreateObject();
    }
    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
}

// ==================== API处理器 ====================

esp_err_t RobotApiServer::HandleDeviceInfo(httpd_req_t* req) {
    auto* server = static_cast<RobotApiServer*>(req->user_ctx);
    if (server == nullptr || server->robot_ == nullptr) {
        SendJsonResponse(req, 500, "服务器错误", "{}");
        return ESP_OK;
    }

    auto* robot = server->robot_;

    // 构建capabilities数组
    auto capabilities = robot->GetCapabilities();
    cJSON* caps_array = cJSON_CreateArray();
    for (const auto& cap : capabilities) {
        cJSON_AddItemToArray(caps_array, cJSON_CreateString(cap.c_str()));
    }

    // 构建data对象
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device_id", robot->GetDeviceId().c_str());
    cJSON_AddStringToObject(data, "model", robot->GetModel().c_str());
    cJSON_AddItemToObject(data, "capabilities", caps_array);
    cJSON_AddStringToObject(data, "fw_version", robot->GetFirmwareVersion().c_str());

    char* data_str = cJSON_PrintUnformatted(data);
    SendJsonResponse(req, 200, "获取成功", data_str);
    
    free(data_str);
    cJSON_Delete(data);

    ESP_LOGI(TAG, "GET /api/v1/device/info - 成功");
    return ESP_OK;
}

esp_err_t RobotApiServer::HandleDeviceAction(httpd_req_t* req) {
    auto* server = static_cast<RobotApiServer*>(req->user_ctx);
    if (server == nullptr || server->robot_ == nullptr) {
        SendJsonResponse(req, 500, "服务器错误", "{}");
        return ESP_OK;
    }

    auto* robot = server->robot_;

    // 读取请求体
    char content[256] = {0};
    int content_len = req->content_len;
    if (content_len >= sizeof(content)) {
        content_len = sizeof(content) - 1;
    }
    
    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
        SendJsonResponse(req, 400, "请求体为空", "{}");
        return ESP_OK;
    }

    // 解析JSON请求
    cJSON* root = cJSON_Parse(content);
    if (root == nullptr) {
        SendJsonResponse(req, 400, "JSON解析失败", "{}");
        return ESP_OK;
    }

    // 获取动作参数
    cJSON* action_json = cJSON_GetObjectItem(root, "action");
    cJSON* steps_json = cJSON_GetObjectItem(root, "steps");
    cJSON* speed_json = cJSON_GetObjectItem(root, "speed");

    if (action_json == nullptr || action_json->valuestring == nullptr) {
        cJSON_Delete(root);
        SendJsonResponse(req, 400, "缺少action参数", "{}");
        return ESP_OK;
    }

    std::string action = action_json->valuestring;
    int steps = (steps_json && cJSON_IsNumber(steps_json)) ? steps_json->valueint : 4;
    int speed = (speed_json && cJSON_IsNumber(speed_json)) ? speed_json->valueint : 1000;

    cJSON_Delete(root);

    // 执行动作
    bool success = robot->ExecuteAction(action, steps, speed);
    
    if (success) {
        std::string action_id = GenerateActionId();
        std::string data = "{\"action_id\":\"" + action_id + "\"}";
        SendJsonResponse(req, 200, "执行成功", data);
        ESP_LOGI(TAG, "POST /api/v1/device/action - 执行动作: %s, steps=%d, speed=%d, action_id=%s", 
                 action.c_str(), steps, speed, action_id.c_str());
    } else {
        SendJsonResponse(req, 400, "动作执行失败", "{}");
        ESP_LOGW(TAG, "POST /api/v1/device/action - 动作执行失败: %s", action.c_str());
    }

    return ESP_OK;
}

esp_err_t RobotApiServer::HandleDeviceStatus(httpd_req_t* req) {
    auto* server = static_cast<RobotApiServer*>(req->user_ctx);
    if (server == nullptr || server->robot_ == nullptr) {
        SendJsonResponse(req, 500, "服务器错误", "{}");
        return ESP_OK;
    }

    auto* robot = server->robot_;

    // 构建状态数据
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action", robot->GetCurrentAction().c_str());
    cJSON_AddBoolToObject(data, "is_idle", robot->IsIdle());
    cJSON_AddNumberToObject(data, "battery", robot->GetBatteryLevel());
    cJSON_AddNumberToObject(data, "volume", robot->GetVolume());

    char* data_str = cJSON_PrintUnformatted(data);
    SendJsonResponse(req, 200, "获取成功", data_str);
    
    free(data_str);
    cJSON_Delete(data);

    ESP_LOGI(TAG, "GET /api/v1/device/status - 成功");
    return ESP_OK;
}

esp_err_t RobotApiServer::HandleDeviceVolumeGet(httpd_req_t* req) {
    auto* server = static_cast<RobotApiServer*>(req->user_ctx);
    if (server == nullptr || server->robot_ == nullptr) {
        SendJsonResponse(req, 500, "服务器错误", "{}");
        return ESP_OK;
    }

    auto* robot = server->robot_;

    // 获取当前音量
    int volume = robot->GetVolume();

    // 构建响应数据
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "volume", volume);

    char* data_str = cJSON_PrintUnformatted(data);
    SendJsonResponse(req, 200, "获取成功", data_str);
    
    free(data_str);
    cJSON_Delete(data);

    ESP_LOGI(TAG, "GET /api/v1/device/volume - 当前音量: %d", volume);
    return ESP_OK;
}

esp_err_t RobotApiServer::HandleDeviceVolumeSet(httpd_req_t* req) {
    auto* server = static_cast<RobotApiServer*>(req->user_ctx);
    if (server == nullptr || server->robot_ == nullptr) {
        SendJsonResponse(req, 500, "服务器错误", "{}");
        return ESP_OK;
    }

    auto* robot = server->robot_;

    // 读取请求体
    char content[256] = {0};
    int content_len = req->content_len;
    if (content_len >= sizeof(content)) {
        content_len = sizeof(content) - 1;
    }
    
    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
        SendJsonResponse(req, 400, "请求体为空", "{}");
        return ESP_OK;
    }

    // 解析JSON请求
    cJSON* root = cJSON_Parse(content);
    if (root == nullptr) {
        SendJsonResponse(req, 400, "JSON解析失败", "{}");
        return ESP_OK;
    }

    // 获取音量参数
    cJSON* volume_json = cJSON_GetObjectItem(root, "volume");
    if (volume_json == nullptr || !cJSON_IsNumber(volume_json)) {
        cJSON_Delete(root);
        SendJsonResponse(req, 400, "缺少volume参数或参数类型错误", "{}");
        return ESP_OK;
    }

    int volume = volume_json->valueint;
    cJSON_Delete(root);

    // 设置音量
    bool success = robot->SetVolume(volume);
    
    if (success) {
        // 返回设置后的音量
        cJSON* data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "volume", robot->GetVolume());
        
        char* data_str = cJSON_PrintUnformatted(data);
        SendJsonResponse(req, 200, "音量设置成功", data_str);
        
        free(data_str);
        cJSON_Delete(data);
        
        ESP_LOGI(TAG, "POST /api/v1/device/volume - 音量已设置为: %d", volume);
    } else {
        SendJsonResponse(req, 500, "音量设置失败", "{}");
        ESP_LOGW(TAG, "POST /api/v1/device/volume - 音量设置失败");
    }

    return ESP_OK;
}
