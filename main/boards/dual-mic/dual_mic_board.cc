#include "wifi_board.h"

#include <cJSON.h>
#include <driver/i2c_master.h>
#include <driver/sdmmc_host.h>
#include <driver/spi_common.h>
#include <esp_camera.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <lvgl.h>
#include <sdmmc_cmd.h>

#include "application.h"
#include "button.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "dual_mic_vector_eye_display.h"
#include "esp32_camera.h"
#include "i2c_device.h"
#include "qmi8658_sensor.h"

#define TAG "DualMicBoard"

namespace {
constexpr uint16_t kFt5x06PrimaryAddress = 0x38;
constexpr uint16_t kFt5x06AlternateAddress = 0x14;
constexpr uint16_t kQmi8658PrimaryAddress = 0x6a;
constexpr uint16_t kQmi8658AlternateAddress = 0x6b;

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
        : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class DualMicAudioCodec : public BoxAudioCodec {
public:
    DualMicAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557 *gpio_expander)
        : BoxAudioCodec(i2c_bus, AUDIO_INPUT_SAMPLE_RATE,
                        AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK,
                        AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                        AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, GPIO_NUM_NC,
                        AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR,
                        AUDIO_INPUT_REFERENCE, AUDIO_INPUT_MICROPHONE_CHANNELS),
          gpio_expander_(gpio_expander) {}

    void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        gpio_expander_->SetOutputState(1, enable ? 1 : 0);
    }

private:
    Pca9557 *gpio_expander_;
};
} // namespace

class DualMicBoard : public WifiBoard {
public:
    DualMicBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeImu();
        InitializeButtons();
        InitializeCamera();
        InitializeSdCard();
        GetBacklight()->RestoreBrightness();
    }

    ~DualMicBoard() override = default;

    AudioCodec *GetAudioCodec() override {
        static DualMicAudioCodec audio_codec(i2c_bus_, gpio_expander_);
        return &audio_codec;
    }

    Display *GetDisplay() override {
        return display_;
    }

    Backlight *GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN,
                                      DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Camera *GetCamera() override {
        return camera_;
    }

    bool UseLegacyTouchSensor() const override {
        return false;
    }

    std::string GetDeviceStatusJson() override {
        auto base_json = WifiBoard::GetDeviceStatusJson();
        auto *root = cJSON_Parse(base_json.c_str());
        if (root == nullptr) {
            return base_json;
        }

        auto *storage = cJSON_CreateObject();
        cJSON_AddBoolToObject(storage, "mounted", sdcard_mounted_);
        cJSON_AddStringToObject(storage, "mount_point", SDCARD_MOUNT_POINT);
        cJSON_AddStringToObject(storage, "bus", "sdmmc-1bit");
        cJSON_AddItemToObject(root, "storage", storage);

        char *printed = cJSON_PrintUnformatted(root);
        std::string json = printed == nullptr ? base_json : printed;
        if (printed != nullptr) {
            cJSON_free(printed);
        }
        cJSON_Delete(root);
        return json;
    }

private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    DualMicVectorEyeDisplay *display_ = nullptr;
    Pca9557 *gpio_expander_ = nullptr;
    Esp32Camera *camera_ = nullptr;
    Qmi8658Sensor *imu_sensor_ = nullptr;
    bool sdcard_mounted_ = false;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        gpio_expander_ = new Pca9557(i2c_bus_, AUDIO_GPIO_EXPANDER_ADDR);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_40;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        gpio_expander_->SetOutputState(0, 0);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new DualMicVectorEyeDisplay(
            panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
            DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY, []() {
              auto &app = Application::GetInstance();
              app.Schedule([]() { Application::GetInstance().OnTouchDetected(); });
            });
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t touch_handle = nullptr;
        esp_lcd_touch_config_t touch_config = {
            .x_max = DISPLAY_HEIGHT,
            .y_max = DISPLAY_WIDTH,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 1,
                .mirror_x = 1,
                .mirror_y = 0,
            },
        };

        esp_lcd_panel_io_handle_t touch_io = nullptr;
        esp_lcd_panel_io_i2c_config_t touch_io_config =
            ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        touch_io_config.scl_speed_hz = 400000;
        esp_err_t err = i2c_master_probe(i2c_bus_, kFt5x06PrimaryAddress, 50);
        if (err != ESP_OK) {
            err = i2c_master_probe(i2c_bus_, kFt5x06AlternateAddress, 50);
            if (err == ESP_OK) {
                touch_io_config.dev_addr = kFt5x06AlternateAddress;
                ESP_LOGI(TAG, "Detected FT5x06 touch controller at 0x%02x",
                         kFt5x06AlternateAddress);
            }
        } else {
            ESP_LOGI(TAG, "Detected FT5x06 touch controller at 0x%02x",
                     kFt5x06PrimaryAddress);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "FT5x06 touch controller not found on I2C, disabling touch");
            return;
        }

        err = esp_lcd_new_panel_io_i2c(i2c_bus_, &touch_io_config, &touch_io);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create touch panel IO: %s",
                     esp_err_to_name(err));
            return;
        }

        err = esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch_config, &touch_handle);
        if (err != ESP_OK || touch_handle == nullptr) {
            ESP_LOGW(TAG, "Failed to initialize FT5x06 touch: %s",
                     esp_err_to_name(err));
            esp_lcd_panel_io_del(touch_io);
            return;
        }

        const lvgl_port_touch_cfg_t lvgl_touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = touch_handle,
        };
        if (lvgl_touch_cfg.disp != nullptr) {
            auto *indev = lvgl_port_add_touch(&lvgl_touch_cfg);
            if (indev == nullptr) {
                ESP_LOGW(TAG, "Failed to register touch with LVGL");
                esp_lcd_touch_del(touch_handle);
                esp_lcd_panel_io_del(touch_io);
            }
        } else {
            ESP_LOGW(TAG, "Touch display is not initialized, skipping touch");
            esp_lcd_touch_del(touch_handle);
            esp_lcd_panel_io_del(touch_io);
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeImu() {
        uint16_t imu_addr = kQmi8658PrimaryAddress;
        esp_err_t err = i2c_master_probe(i2c_bus_, imu_addr, 50);
        if (err != ESP_OK) {
            imu_addr = kQmi8658AlternateAddress;
            err = i2c_master_probe(i2c_bus_, imu_addr, 50);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "QMI8658A not found on I2C, shake detection disabled");
            return;
        }

        ESP_LOGI(TAG, "Detected QMI8658A at 0x%02x", imu_addr);
        imu_sensor_ = new Qmi8658Sensor(i2c_bus_, imu_addr);
        if (!imu_sensor_->Initialize()) {
            ESP_LOGW(TAG, "Failed to initialize QMI8658A");
            delete imu_sensor_;
            imu_sensor_ = nullptr;
            return;
        }

        imu_sensor_->SetShakeCallback([]() {
            Application::GetInstance().Schedule(
                []() { Application::GetInstance().OnShakeDetected(); });
        });
        if (!imu_sensor_->Start()) {
            ESP_LOGW(TAG, "Failed to start QMI8658A polling task");
            delete imu_sensor_;
            imu_sensor_ = nullptr;
        }
    }

    void InitializeCamera() {
        gpio_expander_->SetOutputState(2, 0);

        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_2;
        config.ledc_timer = LEDC_TIMER_2;
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
    }

    void InitializeSdCard() {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = SDCARD_SDMMC_BUS_WIDTH;
        slot_config.clk = SDCARD_SDMMC_CLK_PIN;
        slot_config.cmd = SDCARD_SDMMC_CMD_PIN;
        slot_config.d0 = SDCARD_SDMMC_D0_PIN;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 0,
            .disk_status_check_enable = true,
        };

        sdmmc_card_t *card = nullptr;
        const esp_err_t ret = esp_vfs_fat_sdmmc_mount(
            SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            sdcard_mounted_ = true;
            ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);
            sdmmc_card_print_info(stdout, card);
            return;
        }

        sdcard_mounted_ = false;
        ESP_LOGW(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
    }
};

void BoardTouchAcknowledgeMotion() {}
void BoardTouchEmotionMotion(const char *emotion) {
    (void)emotion;
}
void BoardShakeAcknowledgeMotion() {}

DECLARE_BOARD(DualMicBoard);
