#ifndef BLE_WIFI_PROVISIONER_H
#define BLE_WIFI_PROVISIONER_H

class BleWifiProvisioner {
public:
    static BleWifiProvisioner& GetInstance();
    void Start();
    void Stop();
    bool IsRunning() const;

private:
    BleWifiProvisioner() = default;
    ~BleWifiProvisioner() = default;
    BleWifiProvisioner(const BleWifiProvisioner&) = delete;
    BleWifiProvisioner& operator=(const BleWifiProvisioner&) = delete;
};

#endif // BLE_WIFI_PROVISIONER_H
