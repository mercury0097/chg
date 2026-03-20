#include "ble_wifi_provisioner.h"

#include <cctype>
#include <string>
#include <cstdlib>

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "system_info.h"
#include "wifi_configuration_ap.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {

static const char *TAG = "BleWifiProv";
static const uint16_t kServiceUuid16 = 0xFFF0;
static const uint16_t kRxUuid16 = 0xFFF1;
static const uint16_t kTxUuid16 = 0xFFF2;
static const size_t kMaxJsonSize = 256;
static const int kMaxConnectRetries = 3;
static const TickType_t kRxAssemblyTimeout = pdMS_TO_TICKS(3000);
static const size_t kNotifyChunkSize = 20;
static const TickType_t kNotifyChunkInterval = pdMS_TO_TICKS(25);

static uint8_t g_addr_type = BLE_ADDR_PUBLIC;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_tx_val_handle = 0;
static bool g_notify_enabled = false;
static bool g_started = false;
static bool g_provisioning = false;
static SemaphoreHandle_t g_lock = nullptr;
static std::string g_device_name;
static std::string g_pending_ssid;
static std::string g_pending_password;
static std::string g_last_status = "{\"action\":\"setwifi\",\"status\":0,\"message\":\"idle\"}";
static std::string g_rx_buffer;
static TickType_t g_rx_last_chunk_tick = 0;

static int GapEvent(struct ble_gap_event *event, void *arg);
static int GattAccess(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg);
static void HostTask(void *param);
static void StartAdvertising();
static void NotifyStatus(const std::string &status);
static void NotifySetWifiStatus(int status, const std::string &message);
static bool NotifyRaw(const char *data, size_t len);
static bool IsJsonStructurallyComplete(const std::string &json);
static bool IsWhitespaceOnly(const char *begin, const char *end);
static void HandleProvisionChunk(const std::string &chunk);
static void HandleProvisionRequest(const std::string &payload);
static void ProvisionTask(void *param);
static std::string BuildDeviceName();

static const ble_uuid16_t kServiceUuid = BLE_UUID16_INIT(kServiceUuid16);
static const ble_uuid16_t kRxUuid = BLE_UUID16_INIT(kRxUuid16);
static const ble_uuid16_t kTxUuid = BLE_UUID16_INIT(kTxUuid16);

static struct ble_gatt_chr_def kChars[] = {
    {
        .uuid = &kRxUuid.u,
        .access_cb = GattAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kTxUuid.u,
        .access_cb = GattAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = &g_tx_val_handle,
        .cpfd = nullptr,
    },
    {0},
};

static const struct ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .characteristics = kChars,
    },
    {0},
};

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t lock) : lock_(lock) {
        if (lock_) {
            xSemaphoreTake(lock_, portMAX_DELAY);
        }
    }
    ~LockGuard() {
        if (lock_) {
            xSemaphoreGive(lock_);
        }
    }

private:
    SemaphoreHandle_t lock_;
};

static void OnReset(int reason) {
    ESP_LOGE(TAG, "BLE reset, reason=%d", reason);
}

static void OnSync() {
    ble_hs_id_infer_auto(0, &g_addr_type);
    StartAdvertising();
}

static std::string BuildDeviceName() {
    std::string name = BOARD_NAME;
    std::string mac = SystemInfo::GetMacAddress();
    for (char &ch : mac) {
        if (ch == ':') {
            ch = '-';
        }
    }
    if (!mac.empty()) {
        name += "-" + mac.substr(mac.size() > 5 ? mac.size() - 5 : 0);
    }
    return name;
}

static void StartAdvertising() {
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = reinterpret_cast<const uint8_t *>(g_device_name.c_str());
    fields.name_len = g_device_name.size();
    fields.name_is_complete = 1;
    fields.uuids16 = &kServiceUuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE set adv fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(g_addr_type, nullptr, BLE_HS_FOREVER, &adv_params, GapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE adv start failed: %d", rc);
    }
}

static int GapEvent(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            g_rx_buffer.clear();
            g_rx_last_chunk_tick = 0;
        } else {
            StartAdvertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_notify_enabled = false;
        g_rx_buffer.clear();
        g_rx_last_chunk_tick = 0;
        StartAdvertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        StartAdvertising();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        g_notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "Subscribe event: conn=%d attr=%d notify=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 static_cast<int>(event->subscribe.cur_notify));
        return 0;
    default:
        return 0;
    }
}

static int GattAccess(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    if (ble_uuid_cmp(uuid, &kRxUuid.u) == 0 &&
        ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        int len = OS_MBUF_PKTLEN(ctxt->om);
        if (len <= 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        std::string payload(len, '\0');
        os_mbuf_copydata(ctxt->om, 0, len, payload.data());
        HandleProvisionChunk(payload);
        return 0;
    }

    if (ble_uuid_cmp(uuid, &kTxUuid.u) == 0 &&
        ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        os_mbuf_append(ctxt->om, g_last_status.data(), g_last_status.size());
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void NotifyStatus(const std::string &status) {
    g_last_status = status;
    if (!g_notify_enabled || g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_tx_val_handle == 0) {
        ESP_LOGW(TAG, "Notify skipped: enabled=%d conn=%d handle=%d",
                 static_cast<int>(g_notify_enabled),
                 g_conn_handle,
                 g_tx_val_handle);
        return;
    }

    const size_t total_len = status.size();
    if (total_len <= kNotifyChunkSize) {
        if (NotifyRaw(status.data(), total_len)) {
            ESP_LOGI(TAG, "Notify sent len=%u", static_cast<unsigned>(total_len));
        }
        return;
    }

    const size_t chunk_count = (total_len + kNotifyChunkSize - 1) / kNotifyChunkSize;
    for (size_t i = 0; i < chunk_count; ++i) {
        size_t offset = i * kNotifyChunkSize;
        size_t remaining = total_len - offset;
        size_t chunk_len = remaining > kNotifyChunkSize ? kNotifyChunkSize : remaining;
        if (!NotifyRaw(status.data() + offset, chunk_len)) {
            ESP_LOGW(TAG, "Notify chunk failed %u/%u", static_cast<unsigned>(i + 1), static_cast<unsigned>(chunk_count));
            return;
        }
        ESP_LOGI(TAG, "Notify chunk sent %u/%u len=%u",
                 static_cast<unsigned>(i + 1),
                 static_cast<unsigned>(chunk_count),
                 static_cast<unsigned>(chunk_len));
        if (i + 1 < chunk_count) {
            vTaskDelay(kNotifyChunkInterval);
        }
    }
}

static bool NotifyRaw(const char *data, size_t len) {
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGW(TAG, "Notify alloc failed");
        return false;
    }
    int rc = ble_gattc_notify_custom(g_conn_handle, g_tx_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Notify failed rc=%d len=%u", rc, static_cast<unsigned>(len));
        return false;
    }
    return true;
}

static void NotifySetWifiStatus(int status, const std::string &message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "setwifi");
    cJSON_AddNumberToObject(root, "status", status);
    cJSON_AddStringToObject(root, "message", message.c_str());
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != nullptr) {
        NotifyStatus(json_str);
        free(json_str);
    }
    cJSON_Delete(root);
}

static bool IsJsonStructurallyComplete(const std::string &json) {
    bool in_string = false;
    bool escaping = false;
    int brace_depth = 0;
    bool saw_open_brace = false;

    for (char ch : json) {
        if (escaping) {
            escaping = false;
            continue;
        }
        if (in_string) {
            if (ch == '\\') {
                escaping = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            saw_open_brace = true;
            ++brace_depth;
        } else if (ch == '}') {
            --brace_depth;
            if (brace_depth < 0) {
                return true;
            }
        }
    }
    return saw_open_brace && brace_depth == 0 && !in_string && !escaping;
}

static bool IsWhitespaceOnly(const char *begin, const char *end) {
    for (const char *it = begin; it < end; ++it) {
        if (!std::isspace(static_cast<unsigned char>(*it))) {
            return false;
        }
    }
    return true;
}

static void HandleProvisionChunk(const std::string &chunk) {
    TickType_t now = xTaskGetTickCount();
    if (!g_rx_buffer.empty() && (now - g_rx_last_chunk_tick) > kRxAssemblyTimeout) {
        g_rx_buffer.clear();
    }
    g_rx_last_chunk_tick = now;

    if (chunk.size() > kMaxJsonSize || g_rx_buffer.size() + chunk.size() > kMaxJsonSize) {
        g_rx_buffer.clear();
        NotifySetWifiStatus(2, "payload_too_large");
        return;
    }
    g_rx_buffer += chunk;

    if (!IsJsonStructurallyComplete(g_rx_buffer)) {
        return;
    }

    const char *parse_end = nullptr;
    cJSON *root = cJSON_ParseWithLengthOpts(
        g_rx_buffer.c_str(), g_rx_buffer.size(), &parse_end, 0);
    if (root == nullptr) {
        g_rx_buffer.clear();
        NotifySetWifiStatus(2, "invalid_json");
        return;
    }
    const char *buffer_end = g_rx_buffer.c_str() + g_rx_buffer.size();
    bool has_extra = (parse_end != nullptr) && !IsWhitespaceOnly(parse_end, buffer_end);
    cJSON_Delete(root);
    if (has_extra) {
        g_rx_buffer.clear();
        NotifySetWifiStatus(2, "invalid_json");
        return;
    }

    std::string payload = g_rx_buffer;
    g_rx_buffer.clear();
    HandleProvisionRequest(payload);
}

static void HandleProvisionRequest(const std::string &payload) {
    cJSON *root = cJSON_ParseWithLength(payload.c_str(), payload.size());
    if (!root) {
        NotifySetWifiStatus(2, "invalid_json");
        return;
    }

    cJSON *action_item = cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (cJSON_IsString(action_item) &&
        action_item->valuestring != nullptr &&
        strcmp(action_item->valuestring, "setwifi") != 0) {
        cJSON_Delete(root);
        NotifySetWifiStatus(2, "invalid_action");
        return;
    }

    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring == nullptr) {
        cJSON_Delete(root);
        NotifySetWifiStatus(2, "invalid_ssid");
        return;
    }

    std::string ssid = ssid_item->valuestring;
    std::string password;
    if (cJSON_IsString(password_item) && password_item->valuestring != nullptr) {
        password = password_item->valuestring;
    }
    cJSON_Delete(root);

    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        NotifySetWifiStatus(2, "invalid_params");
        return;
    }

    {
        LockGuard guard(g_lock);
        if (g_provisioning) {
            NotifySetWifiStatus(2, "busy");
            return;
        }
        g_provisioning = true;
        g_pending_ssid = ssid;
        g_pending_password = password;
    }

    if (xTaskCreate(ProvisionTask, "ble_wifi_prov", 4096, nullptr, 5, nullptr) != pdPASS) {
        LockGuard guard(g_lock);
        g_provisioning = false;
        NotifySetWifiStatus(2, "task_failed");
    }
}

static void ProvisionTask(void *param) {
    std::string ssid;
    std::string password;
    {
        LockGuard guard(g_lock);
        ssid = g_pending_ssid;
        password = g_pending_password;
    }

    auto &wifi_ap = WifiConfigurationAp::GetInstance();
    bool ok = false;
    int tries = 0;
    for (tries = 1; tries <= kMaxConnectRetries; ++tries) {
        NotifySetWifiStatus(0, std::to_string(tries));
        ok = wifi_ap.ConnectToWifi(ssid, password);
        if (ok) {
            break;
        }
    }
    if (ok) {
        wifi_ap.Save(ssid, password);
    }

    {
        LockGuard guard(g_lock);
        g_provisioning = false;
    }

    if (ok) {
        std::string ip = wifi_ap.GetStaIpAddress();
        if (ip.empty()) {
            ip = "connected_no_ip";
        }
        NotifySetWifiStatus(1, ip);
        // Give mobile client enough time to receive and process the success notify.
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        NotifySetWifiStatus(2, "connect_failed");
    }

    vTaskDelete(nullptr);
}

static void HostTask(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

} // namespace

BleWifiProvisioner& BleWifiProvisioner::GetInstance() {
    static BleWifiProvisioner instance;
    return instance;
}

bool BleWifiProvisioner::IsRunning() const {
    return g_started;
}

void BleWifiProvisioner::Start() {
    if (g_started) {
        return;
    }
    g_started = true;

    if (!g_lock) {
        g_lock = xSemaphoreCreateMutex();
    }

    g_device_name = BuildDeviceName();

    ble_hs_cfg.reset_cb = OnReset;
    ble_hs_cfg.sync_cb = OnSync;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        g_started = false;
        return;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(g_device_name.c_str());

    ble_gatts_count_cfg(kServices);
    ble_gatts_add_svcs(kServices);

    nimble_port_freertos_init(HostTask);
    ESP_LOGI(TAG, "BLE WiFi provisioning started, name=%s", g_device_name.c_str());
}

void BleWifiProvisioner::Stop() {
    if (!g_started) {
        return;
    }
    nimble_port_stop();
    nimble_port_deinit();
    g_started = false;
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_notify_enabled = false;
}
