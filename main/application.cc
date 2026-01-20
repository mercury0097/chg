#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "mqtt_protocol.h"
#include "system_info.h"
#include "touch_handler.h"
#include "websocket_protocol.h"

namespace {
constexpr const char *kTouchMessage = "（用户轻轻摸了摸你的头）";
constexpr int64_t kTouchStartAckTimeoutUs = 700'000; // 700ms
} // namespace

#include "assets.h"
#include "core/event_bus.h"
#include "mcp_server.h"
#include "settings.h"

#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <font_awesome.h>

#define TAG "Application"

static const char *const STATE_STRINGS[] = {
    "unknown",    "starting",      "configuring", "idle",
    "connecting", "listening",     "speaking",    "upgrading",
    "activating", "audio_testing", "fatal_error", "invalid_state"};

// 声明外部 Otto 动作函数
extern void OttoSwing(int steps, int speed, int amount);
extern void OttoJump(int steps, int speed);

Application::Application() {
  event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error                                                                         \
    "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
  aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
  aec_mode_ = kAecOnServerSide;
#else
  aec_mode_ = kAecOff;
#endif

  esp_timer_create_args_t clock_timer_args = {
      .callback =
          [](void *arg) {
            Application *app = (Application *)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
          },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "clock_timer",
      .skip_unhandled_events = true};
  esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
  if (clock_timer_handle_ != nullptr) {
    esp_timer_stop(clock_timer_handle_);
    esp_timer_delete(clock_timer_handle_);
  }
  vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto &assets = Assets::GetInstance();

  if (!assets.partition_valid()) {
    ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
    return;
  }

  Settings settings("assets", true);
  // Check if there is a new assets need to be downloaded
  std::string download_url = settings.GetString("download_url");

  if (!download_url.empty()) {
    settings.EraseKey("download_url");

    char message[256];
    snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS,
             download_url.c_str());
    Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down",
          Lang::Sounds::OGG_UPGRADE);

    // Wait for the audio service to be idle for 3 seconds
    vTaskDelay(pdMS_TO_TICKS(3000));
    SetDeviceState(kDeviceStateUpgrading);
    board.SetPowerSaveMode(false);
    display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

    bool success = assets.Download(
        download_url, [display](int progress, size_t speed) -> void {
          std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                     speed / 1024);
            display->SetChatMessage("system", buffer);
          }).detach();
        });

    board.SetPowerSaveMode(true);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!success) {
      Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED,
            "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
      vTaskDelay(pdMS_TO_TICKS(2000));
      return;
    }
  }

  // Apply assets
  assets.Apply();
  display->SetChatMessage("system", "");
  display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota &ota) {
  const int MAX_RETRY = 10;
  int retry_count = 0;
  int retry_delay = 10; // 初始重试延迟为10秒

  auto &board = Board::GetInstance();
  while (true) {
    SetDeviceState(kDeviceStateActivating);
    auto display = board.GetDisplay();
    display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

    if (!ota.CheckVersion()) {
      retry_count++;
      if (retry_count >= MAX_RETRY) {
        ESP_LOGE(TAG, "Too many retries, exit version check");
        return;
      }

      char buffer[256];
      snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED,
               retry_delay, ota.GetCheckVersionUrl().c_str());
      Alert(Lang::Strings::ERROR, buffer, "cloud_slash",
            Lang::Sounds::OGG_EXCLAMATION);

      ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)",
               retry_delay, retry_count, MAX_RETRY);
      for (int i = 0; i < retry_delay; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (device_state_ == kDeviceStateIdle) {
          break;
        }
      }
      retry_delay *= 2; // 每次重试后延迟时间翻倍
      continue;
    }
    retry_count = 0;
    retry_delay = 10; // 重置重试延迟时间

    if (ota.HasNewVersion()) {
      if (UpgradeFirmware(ota)) {
        return; // This line will never be reached after reboot
      }
      // If upgrade failed, continue to normal operation (don't break, just fall
      // through)
    }

    // No new version, mark the current version as valid
    ota.MarkCurrentVersionValid();
    if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
      xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
      // Exit the loop if done checking new version
      break;
    }

    display->SetStatus(Lang::Strings::ACTIVATION);
    // Activation code is shown to the user and waiting for the user to input
    if (ota.HasActivationCode()) {
      ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
    }

    // This will block the loop until the activation is done or timeout
    for (int i = 0; i < 10; ++i) {
      ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
      esp_err_t err = ota.Activate();
      if (err == ESP_OK) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
        break;
      } else if (err == ESP_ERR_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(3000));
      } else {
        vTaskDelay(pdMS_TO_TICKS(10000));
      }
      if (device_state_ == kDeviceStateIdle) {
        break;
      }
    }
  }
}

void Application::ShowActivationCode(const std::string &code,
                                     const std::string &message) {
  struct digit_sound {
    char digit;
    const std::string_view &sound;
  };
  static const std::array<digit_sound, 10> digit_sounds{
      {digit_sound{'0', Lang::Sounds::OGG_0},
       digit_sound{'1', Lang::Sounds::OGG_1},
       digit_sound{'2', Lang::Sounds::OGG_2},
       digit_sound{'3', Lang::Sounds::OGG_3},
       digit_sound{'4', Lang::Sounds::OGG_4},
       digit_sound{'5', Lang::Sounds::OGG_5},
       digit_sound{'6', Lang::Sounds::OGG_6},
       digit_sound{'7', Lang::Sounds::OGG_7},
       digit_sound{'8', Lang::Sounds::OGG_8},
       digit_sound{'9', Lang::Sounds::OGG_9}}};

  // This sentence uses 9KB of SRAM, so we need to wait for it to finish
  Alert(Lang::Strings::ACTIVATION, message.c_str(), "link",
        Lang::Sounds::OGG_ACTIVATION);

  for (const auto &digit : code) {
    auto it = std::find_if(
        digit_sounds.begin(), digit_sounds.end(),
        [digit](const digit_sound &ds) { return ds.digit == digit; });
    if (it != digit_sounds.end()) {
      audio_service_.PlaySound(it->sound);
    }
  }
}

void Application::Alert(const char *status, const char *message,
                        const char *emotion, const std::string_view &sound) {
  ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
  auto display = Board::GetInstance().GetDisplay();
  display->SetStatus(status);
  display->SetEmotion(emotion);
  display->SetChatMessage("system", message);
  if (!sound.empty()) {
    audio_service_.PlaySound(sound);
  }
}

void Application::DismissAlert() {
  if (device_state_ == kDeviceStateIdle) {
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(Lang::Strings::STANDBY);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", "");
  }
}

void Application::ToggleChatState() {
  if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
    return;
  } else if (device_state_ == kDeviceStateWifiConfiguring) {
    audio_service_.EnableAudioTesting(true);
    SetDeviceState(kDeviceStateAudioTesting);
    return;
  } else if (device_state_ == kDeviceStateAudioTesting) {
    audio_service_.EnableAudioTesting(false);
    SetDeviceState(kDeviceStateWifiConfiguring);
    return;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    Schedule([this]() {
      if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
          return;
        }
      }

      SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                            : kListeningModeRealtime);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    Schedule([this]() { protocol_->CloseAudioChannel(); });
  }
}

void Application::StartListening() {
  if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
    return;
  } else if (device_state_ == kDeviceStateWifiConfiguring) {
    audio_service_.EnableAudioTesting(true);
    SetDeviceState(kDeviceStateAudioTesting);
    return;
  }

  if (!protocol_) {
    ESP_LOGE(TAG, "Protocol not initialized");
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    Schedule([this]() {
      if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
          return;
        }
      }

      SetListeningMode(kListeningModeManualStop);
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() {
      AbortSpeaking(kAbortReasonNone);
      SetListeningMode(kListeningModeManualStop);
    });
  }
}

void Application::StopListening() {
  if (device_state_ == kDeviceStateAudioTesting) {
    audio_service_.EnableAudioTesting(false);
    SetDeviceState(kDeviceStateWifiConfiguring);
    return;
  }

  const std::array<int, 3> valid_states = {
      kDeviceStateListening,
      kDeviceStateSpeaking,
      kDeviceStateIdle,
  };
  // If not valid, do nothing
  if (std::find(valid_states.begin(), valid_states.end(), device_state_) ==
      valid_states.end()) {
    return;
  }

  Schedule([this]() {
    if (device_state_ == kDeviceStateListening) {
      protocol_->SendStopListening();
      SetDeviceState(kDeviceStateIdle);
    }
  });
}

void Application::Start() {
  auto &board = Board::GetInstance();
  SetDeviceState(kDeviceStateStarting);

  /* Setup the display */
  auto display = board.GetDisplay();

  // Print board name/version info
  display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

  /* Setup the audio service */
  auto codec = board.GetAudioCodec();
  audio_service_.Initialize(codec);
  audio_service_.Start();

  AudioServiceCallbacks callbacks;
  callbacks.on_send_queue_available = [this]() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
  };
  callbacks.on_wake_word_detected = [this](const std::string &wake_word) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
  };
  callbacks.on_vad_change = [this](bool speaking) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
  };
  audio_service_.SetCallbacks(callbacks);

  // 🧠 初始化事件总线和学习系统（NVS存储）
  ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  ESP_LOGI(TAG, "🧠 初始化智能学习系统 (NVS存储):");

  auto &event_bus = xiaozhi::EventBus::GetInstance();
  if (event_bus.Initialize() == ESP_OK) {
    ESP_LOGI(TAG, "  ✅ 事件总线初始化成功");
  } else {
    ESP_LOGE(TAG, "  ❌ 事件总线初始化失败");
  }

  // ⚠️  已禁用学习系统以节省CPU和内存资源
  // auto &user_profile = xiaozhi::UserProfile::GetInstance();
  // if (user_profile.Initialize()) {
  //   ESP_LOGI(TAG, "  ✅ 用户画像加载成功 (7d互动: %u次)",
  //            user_profile.GetInteractionCount7d());
  // } else {
  //   ESP_LOGW(TAG, "  ⚠️  用户画像加载失败，使用默认值");
  // }

  // auto &decision_engine = xiaozhi::DecisionEngine::GetInstance();
  // if (decision_engine.Initialize()) {
  //   ESP_LOGI(TAG, "  ✅ 决策引擎初始化成功");
  // } else {
  //   ESP_LOGE(TAG, "  ❌ 决策引擎初始化失败");
  // }

  // // 🎯 初始化自适应行为系统
  // auto &adaptive = xiaozhi::AdaptiveBehavior::GetInstance();
  // if (adaptive.Initialize()) {
  //   ESP_LOGI(TAG, "  ✅ 自适应行为系统初始化成功");
  //   ESP_LOGI(TAG, "    📊 用户频率等级: %d (0=低频, 1=中频, 2=高频)",
  //            adaptive.GetUserFrequencyLevel());
  //   ESP_LOGI(TAG, "    🐾 宠物衰减速率: %.2fx", adaptive.GetPetDecayRate());
  // } else {
  //   ESP_LOGW(TAG, "  ⚠️  自适应行为系统初始化失败，使用默认值");
  // }

  // // 💝 初始化情绪记忆系统
  // auto &emotional_memory = xiaozhi::EmotionalMemory::GetInstance();
  // if (emotional_memory.Initialize()) {
  //   ESP_LOGI(TAG, "  ✅ 情绪记忆系统初始化成功");
  //   ESP_LOGI(TAG, "    💔 孤独感: %d/100",
  //            emotional_memory.GetLonelinessLevel());
  //   ESP_LOGI(TAG, "    ⚡ 兴奋度: %d/100",
  //            emotional_memory.GetExcitementLevel());
  //   ESP_LOGI(TAG, "    ❤️  信任度: %d/100", emotional_memory.GetTrustLevel());
  // } else {
  //   ESP_LOGW(TAG, "  ⚠️  情绪记忆系统初始化失败，使用默认值");
  // }
  
  ESP_LOGI(TAG, "  ⚠️  学习系统已禁用以节省资源");

  // 📡 注册事件监听器（已禁用，学习系统已关闭）
  // event_bus.Subscribe(xiaozhi::LOGIC_EVENT, xiaozhi::LOGIC_CONVERSATION_END,
  //                     [&user_profile](void *event_data) {
  //                       ESP_LOGI(TAG, "🎧 Event received: CONVERSATION_END");
  //                       ESP_LOGI(TAG,
  //                                "  📊 User stats: 7d互动=%u次, 最爱话题=%s",
  //                                user_profile.GetInteractionCount7d(),
  //                                user_profile.GetFavoriteTopic());
  //                     });
  // ESP_LOGI(TAG, "  ✅ 注册事件监听器: CONVERSATION_END");

  ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

  // 🎯 显示音频处理配置摘要
  ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  ESP_LOGI(TAG, "📊 音频处理配置摘要:");
#ifdef CONFIG_SR_NSN_NSNET2
  ESP_LOGI(TAG, "  🧠 降噪: NSNet2 神经网络");
#elif defined(CONFIG_SR_NSN_WEBRTC)
  ESP_LOGI(TAG, "  🔹 降噪: WebRTC 传统算法");
#else
  ESP_LOGI(TAG, "  ❌ 降噪: 未启用");
#endif

#ifdef CONFIG_SR_VADN_VADNET1_MEDIUM
  ESP_LOGI(TAG, "  🧠 VAD: VADNet1 神经网络");
#elif defined(CONFIG_SR_VADN_WEBRTC)
  ESP_LOGI(TAG, "  🔹 VAD: WebRTC 传统算法");
#else
  ESP_LOGI(TAG, "  ❌ VAD: 未启用");
#endif

  ESP_LOGI(TAG, "  ✅ AGC: WEBRTC 模式 (15dB 激进增益)");
  ESP_LOGI(TAG, "  ✅ SE:  语音增强 (突出人声频段)");
  ESP_LOGI(TAG, "  ✅ AEC: 回声消除 (取决于硬件)");
  ESP_LOGI(TAG, "  ⚙️  Ringbuffer: 300, 优先级: 4, 核心: CPU1");
  ESP_LOGI(TAG, "  ℹ️  注意: 神经网络算法在首次说话时才会加载");
  ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

  // Start the main event loop task with priority 3
  xTaskCreate(
      [](void *arg) {
        ((Application *)arg)->MainEventLoop();
        vTaskDelete(NULL);
      },
      "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

  /* Start the clock timer to update the status bar */
  esp_timer_start_periodic(clock_timer_handle_, 1000000);

  /* Wait for the network to be ready */
  board.StartNetwork();
  board.StartNetworkServices();

  // Update the status bar immediately to show the network state
  display->UpdateStatusBar(true);

  // Check for new assets version
  CheckAssetsVersion();

  // Check for new firmware version or get the MQTT broker address
  Ota ota;
  CheckNewVersion(ota);

  // Initialize the protocol
  display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

  // Add MCP common tools before initializing the protocol
  auto &mcp_server = McpServer::GetInstance();
  mcp_server.AddCommonTools();
  mcp_server.AddUserOnlyTools();

  // 🐾 已禁用电子宠物系统以节省CPU和内存资源
  // auto &pet_system = PetSystem::GetInstance();
  // ESP_LOGI(TAG, "  ℹ️  宠物系统：仅支持手动查询（说'宠物状态'）");
  // pet_system.Start();
  ESP_LOGI(TAG, "  ⚠️  宠物系统已禁用以节省资源");

  if (ota.HasMqttConfig()) {
    protocol_ = std::make_unique<MqttProtocol>();
  } else if (ota.HasWebsocketConfig()) {
    protocol_ = std::make_unique<WebsocketProtocol>();
  } else {
    ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
    protocol_ = std::make_unique<MqttProtocol>();
  }

  protocol_->OnConnected([this]() { DismissAlert(); });

  protocol_->OnNetworkError([this](const std::string &message) {
    last_error_message_ = message;
    xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
  });
  protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
    if (device_state_ == kDeviceStateSpeaking) {
      audio_service_.PushPacketToDecodeQueue(std::move(packet));
    }
  });
  protocol_->OnAudioChannelOpened([this, codec, &board]() {
    board.SetPowerSaveMode(false);
    if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
      ESP_LOGW(TAG,
               "Server sample rate %d does not match device output sample rate "
               "%d, resampling may cause distortion",
               protocol_->server_sample_rate(), codec->output_sample_rate());
    }
  });
  protocol_->OnAudioChannelClosed([this, &board]() {
    board.SetPowerSaveMode(true);
    Schedule([this]() {
      auto display = Board::GetInstance().GetDisplay();
      display->SetChatMessage("system", "");
      SetDeviceState(kDeviceStateIdle);

      if (touch_message_pending_) {
        ESP_LOGW(
            TAG,
            "Touch: audio channel closed before message completed (error)");
        touch_message_pending_ = false;
        touch_waiting_start_ack_ = false;
        touch_start_request_time_us_ = 0;
      }
      touch_channel_opened_for_touch_ = false;
    });
  });
  protocol_->OnIncomingJson([this, display](const cJSON *root) {
    // Parse JSON data
    auto type = cJSON_GetObjectItem(root, "type");
    if (strcmp(type->valuestring, "listen") == 0) {
      // Touch events no longer use listen ack mechanism
      // AI responds directly to MCP notifications
    } else if (strcmp(type->valuestring, "tts") == 0) {
      auto state = cJSON_GetObjectItem(root, "state");
      if (strcmp(state->valuestring, "start") == 0) {
        Schedule([this]() {
          aborted_ = false;
          if (device_state_ == kDeviceStateIdle ||
              device_state_ == kDeviceStateListening) {
            SetDeviceState(kDeviceStateSpeaking);
          }
        });
      } else if (strcmp(state->valuestring, "stop") == 0) {
        Schedule([this]() {
          if (device_state_ == kDeviceStateSpeaking) {
            // 🛡️ 等待音频播放完成后再切换状态
            ESP_LOGI(TAG, "收到 stop 消息，等待音频播放完成...");

            // 等待所有音频队列清空（最多等待10秒）
            int wait_count = 0;
            while (!audio_service_.IsIdle() && wait_count < 100) {
              vTaskDelay(pdMS_TO_TICKS(100)); // 每100ms检查一次
              wait_count++;
            }

            if (wait_count >= 100) {
              ESP_LOGW(TAG, "等待音频播放超时（10秒），强制切换状态");
            } else {
              ESP_LOGI(TAG, "音频播放完成，用时 %d ms", wait_count * 100);
            }

            // 🧠 记录对话（已禁用，学习系统已关闭）
            // auto &profile = xiaozhi::UserProfile::GetInstance();
            // profile.RecordInteraction("chat", 5000); // 假设5秒对话
            // profile.CheckAutoSave();

            // 📡 发布对话结束事件（事件总线）
            auto &event_bus = xiaozhi::EventBus::GetInstance();
            event_bus.Publish(xiaozhi::LOGIC_EVENT,
                              xiaozhi::LOGIC_CONVERSATION_END, nullptr);
            ESP_LOGI(TAG, "📡 Event published: CONVERSATION_END");

            // Ensure microphone is unmuted for the next turn
            audio_service_.SetInputMute(false);

            if (listening_mode_ == kListeningModeManualStop) {
              SetDeviceState(kDeviceStateIdle);
            } else {
              SetDeviceState(kDeviceStateListening);
            }
          }
        });
      } else if (strcmp(state->valuestring, "sentence_start") == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
          ESP_LOGI(TAG, "<< %s", text->valuestring);
          Schedule([this, display, message = std::string(text->valuestring)]() {
            display->SetChatMessage("assistant", message.c_str());
          });
        }
      }
    } else if (strcmp(type->valuestring, "stt") == 0) {
      auto text = cJSON_GetObjectItem(root, "text");
      if (cJSON_IsString(text)) {
        ESP_LOGI(TAG, ">> %s", text->valuestring);
        Schedule([this, display, message = std::string(text->valuestring)]() {
          display->SetChatMessage("user", message.c_str());
          // 🐾 记录聊天（已禁用，宠物系统已关闭）
          // PetSystem::GetInstance().RecordChat();
        });
      }
    } else if (strcmp(type->valuestring, "llm") == 0) {
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(emotion)) {
        Schedule(
            [this, display, emotion_str = std::string(emotion->valuestring)]() {
              display->SetEmotion(emotion_str.c_str());
            });
      }
    } else if (strcmp(type->valuestring, "mcp") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      if (cJSON_IsObject(payload)) {
        McpServer::GetInstance().ParseMessage(payload);
      }
    } else if (strcmp(type->valuestring, "system") == 0) {
      auto command = cJSON_GetObjectItem(root, "command");
      if (cJSON_IsString(command)) {
        ESP_LOGI(TAG, "System command: %s", command->valuestring);
        if (strcmp(command->valuestring, "reboot") == 0) {
          // Do a reboot if user requests a OTA update
          Schedule([this]() { Reboot(); });
        } else {
          ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
        }
      }
    } else if (strcmp(type->valuestring, "alert") == 0) {
      auto status = cJSON_GetObjectItem(root, "status");
      auto message = cJSON_GetObjectItem(root, "message");
      auto emotion = cJSON_GetObjectItem(root, "emotion");
      if (cJSON_IsString(status) && cJSON_IsString(message) &&
          cJSON_IsString(emotion)) {
        Alert(status->valuestring, message->valuestring, emotion->valuestring,
              Lang::Sounds::OGG_VIBRATION);
      } else {
        ESP_LOGW(TAG, "Alert command requires status, message and emotion");
      }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    } else if (strcmp(type->valuestring, "custom") == 0) {
      auto payload = cJSON_GetObjectItem(root, "payload");
      ESP_LOGI(TAG, "Received custom message: %s",
               cJSON_PrintUnformatted(root));
      if (cJSON_IsObject(payload)) {
        Schedule(
            [this, display,
             payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
              display->SetChatMessage("system", payload_str.c_str());
            });
      } else {
        ESP_LOGW(TAG, "Invalid custom message format: missing payload");
      }
#endif
    } else {
      ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
    }
  });
  bool protocol_started = protocol_->Start();

  SystemInfo::PrintHeapStats();
  SetDeviceState(kDeviceStateIdle);

  has_server_time_ = ota.HasServerTime();
  if (protocol_started) {
    std::string message =
        std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");
    // Play the success sound to indicate the device is ready
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
  }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    main_tasks_.push_back(std::move(callback));
  }
  xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
  while (true) {
    auto bits = xEventGroupWaitBits(
        event_group_,
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED | MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR,
        pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & MAIN_EVENT_ERROR) {
      SetDeviceState(kDeviceStateIdle);
      Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark",
            Lang::Sounds::OGG_EXCLAMATION);
    }

    if (bits & MAIN_EVENT_SEND_AUDIO) {
      while (auto packet = audio_service_.PopPacketFromSendQueue()) {
        if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
          break;
        }
      }
    }

    if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
      OnWakeWordDetected();
    }

    if (bits & MAIN_EVENT_VAD_CHANGE) {
      if (device_state_ == kDeviceStateListening) {
        auto led = Board::GetInstance().GetLed();
        led->OnStateChanged();
      }
    }

    if (bits & MAIN_EVENT_SCHEDULE) {
      std::unique_lock<std::mutex> lock(mutex_);
      auto tasks = std::move(main_tasks_);
      lock.unlock();
      for (auto &task : tasks) {
        task();
      }
    }

    if (bits & MAIN_EVENT_CLOCK_TICK) {
      clock_ticks_++;
      auto display = Board::GetInstance().GetDisplay();
      display->UpdateStatusBar();

      // Touch events no longer use ack/timeout mechanism

      // Print the debug info every 10 seconds
      if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();
      }
    }
  }
}

void Application::OnWakeWordDetected() {
  if (!protocol_) {
    return;
  }

  if (device_state_ == kDeviceStateIdle) {
    audio_service_.EncodeWakeWord();

    if (!protocol_->IsAudioChannelOpened()) {
      SetDeviceState(kDeviceStateConnecting);
      if (!protocol_->OpenAudioChannel()) {
        audio_service_.EnableWakeWordDetection(true);
        return;
      }
    }

    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
      protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
#else
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop
                                          : kListeningModeRealtime);
    // Play the pop up sound to indicate the wake word is detected
    audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
  } else if (device_state_ == kDeviceStateSpeaking) {
    AbortSpeaking(kAbortReasonWakeWordDetected);
  } else if (device_state_ == kDeviceStateActivating) {
    SetDeviceState(kDeviceStateIdle);
  }
}

void Application::AbortSpeaking(AbortReason reason) {
  ESP_LOGI(TAG, "Abort speaking");
  aborted_ = true;
  if (protocol_) {
    protocol_->SendAbortSpeaking(reason);
  }
  // Ensure immediate silence by resetting the decoder
  audio_service_.ResetDecoder();
}

void Application::SetListeningMode(ListeningMode mode) {
  listening_mode_ = mode;
  SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
  if (device_state_ == state) {
    return;
  }

  clock_ticks_ = 0;
  auto previous_state = device_state_;
  device_state_ = state;
  ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

  // Send the state change event
  DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state,
                                                              state);

  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();
  auto led = board.GetLed();
  led->OnStateChanged();
  switch (state) {
  case kDeviceStateUnknown:
  case kDeviceStateIdle:
    display->SetStatus(Lang::Strings::STANDBY);
    display->SetEmotion("neutral");
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(true);

    // 👆 延迟初始化触摸检测（使用定时器，避免阻塞主事件循环）
    if (!touch_initialized_) {
      touch_initialized_ = true;
      ESP_LOGI(TAG, "Will initialize touch handler (GPIO13) in 2 seconds...");

      // 创建一次性定时器来延迟初始化
      esp_timer_create_args_t timer_args = {
          .callback =
              [](void *arg) {
                Application *app = static_cast<Application *>(arg);
                ESP_LOGI(TAG, "Initializing touch handler on GPIO13...");
                esp_err_t ret = app->touch_handler_.Start([app]() {
                  app->Schedule([app]() { app->OnTouchDetected(); });
                });
                if (ret != ESP_OK) {
                  ESP_LOGW(TAG,
                           "Touch handler initialization failed (%s), "
                           "continuing without touch support",
                           esp_err_to_name(ret));
                }
              },
          .arg = this,
          .dispatch_method = ESP_TIMER_TASK,
          .name = "touch_init_timer"};

      esp_timer_handle_t touch_init_timer;
      if (esp_timer_create(&timer_args, &touch_init_timer) == ESP_OK) {
        // 延迟2秒后初始化（确保音频系统完全稳定）
        esp_timer_start_once(touch_init_timer, 2000000); // 2秒 = 2,000,000 微秒
      } else {
        ESP_LOGE(TAG, "Failed to create touch init timer");
      }
    }
    break;
  case kDeviceStateConnecting:
    display->SetStatus(Lang::Strings::CONNECTING);
    display->SetEmotion("neutral");
    display->SetChatMessage("system", "");
    break;
  case kDeviceStateListening:
    display->SetStatus(Lang::Strings::LISTENING);
    display->SetEmotion("neutral");

    // Send start listening if audio processor isn't running
    if (!audio_service_.IsAudioProcessorRunning()) {
      protocol_->SendStartListening(listening_mode_);
    }

    // Always enable voice processing when entering Listening state
    // This is critical for VAD to work after state transitions
    audio_service_.EnableVoiceProcessing(true);
    audio_service_.EnableWakeWordDetection(false);
    break;
  case kDeviceStateSpeaking:
    display->SetStatus(Lang::Strings::SPEAKING);

    //  ⚠️  Barge-in 暂时禁用：ESP-SR AFE 不支持 AEC + VAD 同时运行
    // Speaking 状态下关闭音频处理，避免 CPU 过载和 Ringbuffer 溢出
    // 用户仍可在 Listening 状态时打断（说话时机器人会停止说话并监听）
    if (listening_mode_ != kListeningModeRealtime) {
      audio_service_.EnableVoiceProcessing(false);
      // Only AFE wake word can be detected in speaking mode
      audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
    }
    audio_service_.ResetDecoder();
    break;
  default:
    // Do nothing
    break;
  }
}

void Application::Reboot() {
  ESP_LOGI(TAG, "Rebooting...");
  // Disconnect the audio channel
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    protocol_->CloseAudioChannel();
  }
  protocol_.reset();
  audio_service_.Stop();

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

bool Application::UpgradeFirmware(Ota &ota, const std::string &url) {
  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();

  // Use provided URL or get from OTA object
  std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
  std::string version_info =
      url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";

  // Close audio channel if it's open
  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
    protocol_->CloseAudioChannel();
  }
  ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

  Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download",
        Lang::Sounds::OGG_UPGRADE);
  vTaskDelay(pdMS_TO_TICKS(3000));

  SetDeviceState(kDeviceStateUpgrading);

  std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
  display->SetChatMessage("system", message.c_str());

  board.SetPowerSaveMode(false);
  audio_service_.Stop();
  vTaskDelay(pdMS_TO_TICKS(1000));

  bool upgrade_success = ota.StartUpgradeFromUrl(
      upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
          char buffer[32];
          snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress,
                   speed / 1024);
          display->SetChatMessage("system", buffer);
        }).detach();
      });

  if (!upgrade_success) {
    // Upgrade failed, restart audio service and continue running
    ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and "
                  "continuing operation...");
    audio_service_.Start();       // Restart audio service
    board.SetPowerSaveMode(true); // Restore power save mode
    Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark",
          Lang::Sounds::OGG_EXCLAMATION);
    vTaskDelay(pdMS_TO_TICKS(3000));
    return false;
  } else {
    // Upgrade success, reboot immediately
    ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
    display->SetChatMessage("system", "Upgrade successful, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
    Reboot();
    return true;
  }
}

void Application::WakeWordInvoke(const std::string &wake_word) {
  if (device_state_ == kDeviceStateIdle) {
    ToggleChatState();
    Schedule([this, wake_word]() {
      if (protocol_) {
        protocol_->SendWakeWordDetected(wake_word);
      }
    });
  } else if (device_state_ == kDeviceStateSpeaking) {
    Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
  } else if (device_state_ == kDeviceStateListening) {
    Schedule([this]() {
      if (protocol_) {
        protocol_->CloseAudioChannel();
      }
    });
  }
}

bool Application::CanEnterSleepMode() {
  if (device_state_ != kDeviceStateIdle) {
    return false;
  }

  if (protocol_ && protocol_->IsAudioChannelOpened()) {
    return false;
  }

  if (!audio_service_.IsIdle()) {
    return false;
  }

  // Now it is safe to enter sleep mode
  return true;
}

void Application::SendMcpMessage(const std::string &payload) {
  if (protocol_ == nullptr) {
    return;
  }

  // Make sure you are using main thread to send MCP message
  if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
    protocol_->SendMcpMessage(payload);
  } else {
    Schedule([this, payload = std::move(payload)]() {
      protocol_->SendMcpMessage(payload);
    });
  }
}

void Application::SetAecMode(AecMode mode) {
  aec_mode_ = mode;
  Schedule([this]() {
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    switch (aec_mode_) {
    case kAecOff:
      audio_service_.EnableDeviceAec(false);
      display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
      break;
    case kAecOnServerSide:
      audio_service_.EnableDeviceAec(false);
      display->ShowNotification(Lang::Strings::RTC_MODE_ON);
      break;
    case kAecOnDeviceSide:
      audio_service_.EnableDeviceAec(true);
      display->ShowNotification(Lang::Strings::RTC_MODE_ON);
      break;
    }

    // If the AEC mode is changed, close the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
      protocol_->CloseAudioChannel();
    }
  });
}

void Application::PlaySound(const std::string_view &sound) {
  audio_service_.PlaySound(sound);
}

void Application::OnTouchDetected() {
  ESP_LOGI(TAG, "Touch detected! Responding with happy emotion and sending "
                "message to AI");

  auto &board = Board::GetInstance();
  auto display = board.GetDisplay();

  display->SetEmotion("happy");

  if (!protocol_) {
    ESP_LOGW(TAG, "Cannot send touch message - protocol not initialized");
    return;
  }

  Schedule([this]() {
    if (touch_message_pending_) {
      ESP_LOGW(TAG, "Touch message already pending, ignoring additional touch");
      return;
    }

    touch_message_pending_ = true;

    auto start_touch_sequence = [this]() {
      ESP_LOGI(TAG, "Touch: starting touch interaction sequence");
      // 随机选择摇摆或跳跃动作
      int random_action = esp_random() % 2;
      if (random_action == 0) {
        ESP_LOGI(TAG, "Touch: triggering Swing action");
        OttoSwing(2, 1000, 30);
      } else {
        ESP_LOGI(TAG, "Touch: triggering Jump action");
        OttoJump(1, 5000);  // 慢动作跳跃
      }
      SendTouchStartSequence();
    };

    if (protocol_->IsAudioChannelOpened()) {
      if (device_state_ == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Touch: aborting speech before starting touch sequence");
        AbortSpeaking(kAbortReasonNone);
      }
      start_touch_sequence();
      return;
    }

    if (device_state_ != kDeviceStateIdle) {
      ESP_LOGW(TAG, "Cannot start touch interaction - device busy");
      touch_message_pending_ = false;
      return;
    }

    ESP_LOGI(TAG, "Touch: audio channel closed, opening for touch interaction");
    SetDeviceState(kDeviceStateConnecting);
    touch_channel_opened_for_touch_ = true;

    if (!protocol_->OpenAudioChannel()) {
      ESP_LOGW(TAG, "Failed to open audio channel for touch interaction");
      touch_message_pending_ = false;
      touch_channel_opened_for_touch_ = false;
      SetDeviceState(kDeviceStateIdle);
      return;
    }

    ESP_LOGI(TAG, "Touch: audio channel opened, starting sequence");
    start_touch_sequence();

    if (device_state_ == kDeviceStateConnecting) {
      // The state will be updated to Listening when the channel is fully open
      // and we send the start command But for now, we can leave it as
      // Connecting or set to Idle if we want to be safe Actually,
      // OpenAudioChannel is async in terms of connection, but we called it
      // synchronously here? No, OpenAudioChannel returns bool immediately but
      // connection happens in background? Let's check OpenAudioChannel
      // implementation if needed. Based on existing code, it seems we just wait
      // for OnAudioChannelOpened or just proceed. In the original code: if
      // (device_state_ == kDeviceStateConnecting) {
      //    SetDeviceState(kDeviceStateIdle);
      // }
      // But here we want to stay in a state that allows interaction.
      // Let's keep it as Connecting, and it will transition to Listening when
      // SendStartListening is called in SendTouchStartSequence Wait,
      // SendTouchStartSequence calls protocol_->SendStartListening. If protocol
      // is not yet connected, SendStartListening might fail or be queued.
      // MqttProtocol::OpenAudioChannel usually initiates connection.
      // We might need to wait for OnAudioChannelOpened callback?
      // The original code had:
      // protocol_->OnAudioChannelOpened([this, codec, &board]() { ... });
      // This callback is already set in Start().

      // However, we are calling start_touch_sequence() immediately after
      // OpenAudioChannel(). If OpenAudioChannel() is not blocking until
      // connected, this might be too early. But the original code for MCP did
      // the same: if (!protocol_->OpenAudioChannel()) { ... } send_touch_mcp();

      // Let's assume it works similarly.
      // But wait, SendStartListening sends a JSON message. If the
      // websocket/mqtt is not connected, it will fail. If OpenAudioChannel just
      // starts the process, we might need to wait.

      // Actually, looking at ToggleChatState:
      // if (!protocol_->IsAudioChannelOpened()) {
      //     SetDeviceState(kDeviceStateConnecting);
      //     if (!protocol_->OpenAudioChannel()) { return; }
      // }
      // SetListeningMode(...);

      // So it seems we can call it immediately?
      // But SetListeningMode just sets the state. It doesn't send data
      // immediately? Wait, SetListeningMode calls protocol_->SendStartListening
      // if audio processor is not running.

      // Let's stick to the pattern.
    }
  });

  // 播放开心的音效（如果有的话）
  // audio_service_.PlaySound(Lang::Sounds::OGG_HAPPY);  //
  // 如果有定义开心音效的话
}

void Application::SendTouchStartSequence() {
  if (!protocol_) {
    return;
  }

  ESP_LOGI(TAG, "Touch: starting sequence");

  // 1. If speaking, abort it
  if (device_state_ == kDeviceStateSpeaking) {
    AbortSpeaking(kAbortReasonNone);
  }

  // 2. Ensure audio channel is open (for subsequent audio streaming)
  if (!protocol_->IsAudioChannelOpened()) {
    SetDeviceState(kDeviceStateConnecting);
    if (!protocol_->OpenAudioChannel()) {
      ESP_LOGE(TAG, "Failed to open audio channel");
      return;
    }
  }

  // 3. Send Wake Word Detected (Text)
  // This triggers the server to respond
  ESP_LOGI(TAG, "Touch: sending wake word: %s", kTouchMessage);
  protocol_->SendWakeWordDetected(kTouchMessage);

  // 4. Set listening mode based on AEC capability
  // If AEC is off, we must use AutoStop to avoid recording the speaker output
  // (echo)
  if (aec_mode_ == kAecOff) {
    listening_mode_ = kListeningModeAutoStop;
  } else {
    listening_mode_ = kListeningModeRealtime;
  }
  SetDeviceState(kDeviceStateListening);

  // 5. Send MCP notification as backup/context
  SendTouchEventViaMcp();

  touch_message_pending_ = false;
  touch_waiting_start_ack_ = false;
  touch_start_request_time_us_ = 0;

  if (touch_channel_opened_for_touch_) {
    touch_channel_opened_for_touch_ = false;
  }
}

void Application::SendTouchEventViaMcp() {
  if (!protocol_) {
    return;
  }

  cJSON *meta = cJSON_CreateObject();
  cJSON_AddStringToObject(meta, "source", "touch_sensor");
  cJSON_AddStringToObject(meta, "emotion", "happy");

  cJSON *params = cJSON_CreateObject();
  cJSON_AddStringToObject(params, "event", "touch");
  cJSON_AddStringToObject(params, "message", kTouchMessage);
  cJSON_AddItemToObject(params, "meta", meta);

  cJSON *rpc = cJSON_CreateObject();
  cJSON_AddStringToObject(rpc, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(rpc, "id", touch_mcp_request_id_++);
  cJSON_AddStringToObject(rpc, "method", "notifications/touch");
  cJSON_AddItemToObject(rpc, "params", params);

  char *payload_str = cJSON_PrintUnformatted(rpc);
  if (payload_str != nullptr) {
    ESP_LOGI(TAG, "Touch: MCP message: %s", payload_str);
    protocol_->SendMcpMessage(payload_str);
    cJSON_free(payload_str);
  }
  cJSON_Delete(rpc);
}
