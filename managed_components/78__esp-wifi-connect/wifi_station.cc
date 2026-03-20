#include "wifi_station.h"
#include <cstring>
#include <algorithm>
#include <unordered_set>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include "nvs_flash.h"
#include <esp_netif.h>
#include <esp_system.h>
#include "ssid_manager.h"

#define TAG "WifiStation"
#define WIFI_EVENT_CONNECTED BIT0
#define MAX_RECONNECT_COUNT 8

static constexpr int kInitialReconnectDelayMs = 500;
static constexpr int kMaxReconnectDelayMs = 8000;
static constexpr int kScanRetryDelayMs = 2000;
static constexpr int kFastReconnectLimit = 2;
static constexpr int kMinCandidateRssi = -88;
static constexpr int kWeakRssiThreshold = -70;

static bool ShouldRescanByReason(wifi_err_reason_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
            return true;
        default:
            return false;
    }
}

WifiStation& WifiStation::GetInstance() {
    static WifiStation instance;
    return instance;
}

WifiStation::WifiStation() {
    // Create the event group
    event_group_ = xEventGroupCreate();

    max_tx_power_ = 0;
    remember_bssid_ = 0;
    sleep_mode_ = 0;
    reconnect_delay_ms_ = kInitialReconnectDelayMs;

    // 读取配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        err = nvs_get_i8(nvs, "max_tx_power", &max_tx_power_);
        if (err != ESP_OK) {
            max_tx_power_ = 0;
        }

        err = nvs_get_u8(nvs, "remember_bssid", &remember_bssid_);
        if (err != ESP_OK) {
            remember_bssid_ = 0;
        }

        err = nvs_get_u8(nvs, "sleep_mode", &sleep_mode_);
        if (err != ESP_OK) {
            // Default to disabled for better weak-network stability.
            sleep_mode_ = 0;
        }

        nvs_close(nvs);
    } else {
        ESP_LOGW(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
}

WifiStation::~WifiStation() {
    vEventGroupDelete(event_group_);
}

void WifiStation::AddAuth(const std::string &&ssid, const std::string &&password) {
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.AddSsid(ssid, password);
}

void WifiStation::Stop() {
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
    if (reconnect_timer_handle_ != nullptr) {
        esp_timer_stop(reconnect_timer_handle_);
        esp_timer_delete(reconnect_timer_handle_);
        reconnect_timer_handle_ = nullptr;
    }

    esp_wifi_scan_stop();
    
    // 取消注册事件处理程序
    if (instance_any_id_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_));
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_));
        instance_got_ip_ = nullptr;
    }

    // Reset the WiFi stack
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    if (station_netif_ != nullptr) {
        esp_netif_destroy(station_netif_);
        station_netif_ = nullptr;
    }

    // Clear event group bits to prevent WaitForConnected from returning prematurely on restart
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    connect_queue_.clear();
    reconnect_count_ = 0;
    reconnect_delay_ms_ = kInitialReconnectDelayMs;
    disable_bssid_for_next_connect_ = false;
}

void WifiStation::OnScanBegin(std::function<void()> on_scan_begin) {
    on_scan_begin_ = on_scan_begin;
}

void WifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect) {
    on_connect_ = on_connect;
}

void WifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected) {
    on_connected_ = on_connected;
}

void WifiStation::Start() {
    // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create the default event loop
    station_netif_ = esp_netif_create_default_wifi_sta();

    // Initialize the WiFi stack in station mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiStation::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiStation::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (max_tx_power_ != 0) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
    }
    ApplyPowerSavePolicy();

    // Setup the timer to scan WiFi
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            esp_wifi_scan_start(nullptr, false);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiScanTimer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));

    esp_timer_create_args_t reconnect_timer_args = {
        .callback = [](void* arg) {
            auto* self = static_cast<WifiStation*>(arg);
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Reconnect failed to start: %s", esp_err_to_name(err));
                self->ScheduleScan(kScanRetryDelayMs);
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiReconnectTimer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_timer_args, &reconnect_timer_handle_));
}

bool WifiStation::WaitForConnected(int timeout_ms) {
    auto bits = xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    return (bits & WIFI_EVENT_CONNECTED) != 0;
}

void WifiStation::HandleScanResult() {
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    connect_queue_.clear();
    if (ap_num == 0) {
        ESP_LOGI(TAG, "Scan done, no AP found");
        ScheduleScan(10 * 1000);
        return;
    }

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
    if (ap_records == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate scan buffer");
        ScheduleScan(kScanRetryDelayMs);
        return;
    }
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);
    // sort by rssi descending
    std::sort(ap_records, ap_records + ap_num, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
        return a.rssi > b.rssi;
    });

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    std::unordered_set<std::string> selected_ssids;
    for (int i = 0; i < ap_num; i++) {
        auto ap_record = ap_records[i];
        if (ap_record.rssi < kMinCandidateRssi) {
            continue;
        }
        auto it = std::find_if(ssid_list.begin(), ssid_list.end(), [ap_record](const SsidItem& item) {
            return strcmp((char *)ap_record.ssid, item.ssid.c_str()) == 0;
        });
        if (it != ssid_list.end() && selected_ssids.insert(it->ssid).second) {
            ESP_LOGI(TAG, "Found AP: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x, RSSI: %d, Channel: %d, Authmode: %d",
                (char *)ap_record.ssid, 
                ap_record.bssid[0], ap_record.bssid[1], ap_record.bssid[2],
                ap_record.bssid[3], ap_record.bssid[4], ap_record.bssid[5],
                ap_record.rssi, ap_record.primary, ap_record.authmode);
            WifiApRecord record = {
                .ssid = it->ssid,
                .password = it->password,
                .channel = ap_record.primary,
                .authmode = ap_record.authmode
            };
            memcpy(record.bssid, ap_record.bssid, 6);
            connect_queue_.push_back(record);
        }
    }
    free(ap_records);

    if (connect_queue_.empty()) {
        ESP_LOGI(TAG, "Wait for next scan");
        ScheduleScan(10 * 1000);
        return;
    }

    StartConnect();
}

void WifiStation::StartConnect() {
    auto ap_record = connect_queue_.front();
    connect_queue_.erase(connect_queue_.begin());
    ssid_ = ap_record.ssid;
    password_ = ap_record.password;

    if (on_connect_) {
        on_connect_(ssid_);
    }

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ap_record.ssid.c_str());
    strcpy((char *)wifi_config.sta.password, ap_record.password.c_str());
    if (remember_bssid_ && !disable_bssid_for_next_connect_) {
        wifi_config.sta.channel = ap_record.channel;
        memcpy(wifi_config.sta.bssid, ap_record.bssid, 6);
        wifi_config.sta.bssid_set = true;
    } else {
        wifi_config.sta.bssid_set = false;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    reconnect_count_ = 0;
    reconnect_delay_ms_ = kInitialReconnectDelayMs;
    ESP_ERROR_CHECK(esp_wifi_connect());
}

int8_t WifiStation::GetRssi() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.rssi;
}

uint8_t WifiStation::GetChannel() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.primary;
}

bool WifiStation::IsConnected() {
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void WifiStation::SetPowerSaveMode(bool enabled) {
    ESP_ERROR_CHECK(esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
}

void WifiStation::ScheduleScan(int delay_ms) {
    if (timer_handle_ == nullptr) {
        return;
    }
    esp_timer_stop(timer_handle_);
    esp_timer_start_once(timer_handle_, static_cast<uint64_t>(delay_ms) * 1000);
}

void WifiStation::ScheduleReconnect(int delay_ms) {
    if (reconnect_timer_handle_ == nullptr) {
        return;
    }
    esp_timer_stop(reconnect_timer_handle_);
    esp_timer_start_once(reconnect_timer_handle_, static_cast<uint64_t>(delay_ms) * 1000);
}

void WifiStation::ApplyPowerSavePolicy() {
    wifi_ps_type_t mode = WIFI_PS_NONE;
    if (sleep_mode_ != 0) {
        mode = WIFI_PS_MIN_MODEM;

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.rssi <= kWeakRssiThreshold) {
            mode = WIFI_PS_NONE;
            ESP_LOGI(TAG, "Weak signal (RSSI=%d), disable power save", ap_info.rssi);
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_ps(mode));
}

// Static event handler functions
void WifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_scan_start(nullptr, false);
        if (this_->on_scan_begin_) {
            this_->on_scan_begin_();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        this_->HandleScanResult();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        auto *event = reinterpret_cast<wifi_event_sta_disconnected_t *>(event_data);
        wifi_err_reason_t reason = WIFI_REASON_UNSPECIFIED;
        if (event != nullptr) {
            reason = static_cast<wifi_err_reason_t>(event->reason);
        }
        ESP_LOGW(TAG, "Disconnected from %s, reason=%d, reconnect_count=%d",
                 this_->ssid_.c_str(),
                 reason,
                 this_->reconnect_count_);

        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);

        bool should_rescan = ShouldRescanByReason(reason) || this_->reconnect_count_ >= kFastReconnectLimit;
        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT && !should_rescan) {
            this_->reconnect_count_++;
            int delay_ms = std::min(this_->reconnect_delay_ms_, kMaxReconnectDelayMs);
            ESP_LOGI(TAG, "Reconnect %s in %d ms (attempt %d/%d)",
                     this_->ssid_.c_str(), delay_ms, this_->reconnect_count_, MAX_RECONNECT_COUNT);
            this_->ScheduleReconnect(delay_ms);
            this_->reconnect_delay_ms_ = std::min(this_->reconnect_delay_ms_ * 2, kMaxReconnectDelayMs);
            return;
        }

        this_->disable_bssid_for_next_connect_ = true;
        this_->reconnect_count_ = 0;
        this_->reconnect_delay_ms_ = kInitialReconnectDelayMs;

        if (!this_->connect_queue_.empty()) {
            this_->StartConnect();
            return;
        }
        
        ESP_LOGI(TAG, "Rescan WiFi after disconnect");
        this_->ScheduleScan(kScanRetryDelayMs);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    }
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());
    
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;
    this_->reconnect_delay_ms_ = kInitialReconnectDelayMs;
    this_->disable_bssid_for_next_connect_ = false;
    this_->ApplyPowerSavePolicy();
}
