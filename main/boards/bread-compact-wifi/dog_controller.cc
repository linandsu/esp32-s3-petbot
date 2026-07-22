#include "dog_controller.h"

#include "application.h"
#include "device_state.h"
#include "mcp_server.h"
#include "board.h"
#include "display.h"
#include "sntp_clock.h"
#include "web_control_server.h"

#include <esp_log.h>

#define TAG "DogController"

// 待机超过这个时长（毫秒）之后，屏幕从"呼呼大睡表情"切换到"时间锁屏"
#define LOCK_SCREEN_IDLE_MS 30000

DogController* DogController::instance_ = nullptr;

DogController::DogController(gpio_num_t servo_io_1, gpio_num_t servo_io_2, gpio_num_t servo_io_3, gpio_num_t servo_io_4) {
    instance_ = this;
    SntpClock::EnsureStarted();

    dog_.InitializeDog(servo_io_1, servo_io_2, servo_io_3, servo_io_4);
    dog_.RequestAction(kActionStateSleep);

    esp_timer_create_args_t lock_timer_args = {
        .callback = &DogController::OnLockScreenTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_lock",
    };
    ESP_ERROR_CHECK(esp_timer_create(&lock_timer_args, &lock_screen_timer_));

    RegisterStateChangeListener();
    RegisterMcpTools();
}

bool DogController::ExecuteAction(const std::string& action) {
    if (action == "walk") dog_.RequestAction(kActionStateWalk);
    else if (action == "walk_back") dog_.RequestAction(kActionStateWalkBack);
    else if (action == "stand") dog_.RequestAction(kActionStateStand);
    else if (action == "sitdown") dog_.RequestAction(kActionStateSitdown);
    else if (action == "sleep") dog_.RequestAction(kActionStateSleep);
    else if (action == "turn_left") dog_.RequestAction(kActionStateTurnLeft);
    else if (action == "turn_right") dog_.RequestAction(kActionStateTurnRight);
    else if (action == "wave") dog_.RequestAction(kActionStateWave);
    else if (action == "stop") dog_.RequestAction(kActionStateStop);
    else return false;

    current_action_ = action;
    return true;
}

namespace {
// 只有真正在"互动"的状态才应该显示清醒的睁眼表情，其余（包括开机中/配网中/
// 联网中）都应该保持睡觉表情——不然开机联网那一两秒会先冒出一次清醒表情，
// 联网成功进入 Idle 后又立刻切回睡觉，看起来像"闪了一下大眼睛"。
bool IsAwakeState(DeviceState state) {
    return state == kDeviceStateListening ||
           state == kDeviceStateSpeaking ||
           state == kDeviceStateActivating ||
           state == kDeviceStateAudioTesting;
}

bool IsStartupState(DeviceState state) {
    return state == kDeviceStateUnknown ||
           state == kDeviceStateStarting ||
           state == kDeviceStateWifiConfiguring ||
           state == kDeviceStateActivating ||
           state == kDeviceStateUpgrading;
}
} // namespace

void DogController::RegisterStateChangeListener() {
    Application::GetInstance().AddStateChangeListener(
        [this](DeviceState old_state, DeviceState new_state) {
            HandleDeviceStateChanged(old_state, new_state);
        });
}

void DogController::HandleDeviceStateChanged(DeviceState old_state, DeviceState new_state) {
    auto display = Board::GetInstance().GetDisplay();
    StopLockScreenTimer();

    if (!startup_complete_) {
        if (new_state == kDeviceStateActivating) {
            startup_activation_seen_ = true;
        }

        if (new_state == kDeviceStateIdle && startup_activation_seen_) {
            startup_complete_ = true;
            if (display) {
                display->ShowDogSleepFace();
            }
            StartLockScreenTimer();
        } else if (display) {
            // Retain the standard Xiaozhi startup UI until networking and
            // server activation have genuinely completed.
            display->ShowDogStartup();
        }
        return;
    }

    const bool was_awake = IsAwakeState(old_state);
    const bool is_awake = IsAwakeState(new_state);

    if (is_awake) {
        if (!was_awake) {
            ExecuteAction("stand");
        }
        if (display) {
            display->ShowDogFace();
            if (new_state == kDeviceStateListening) {
                display->SetEmotion("listening");
            } else if (new_state == kDeviceStateSpeaking) {
                display->SetEmotion("neutral");
            }
        }
    } else if (was_awake || new_state == kDeviceStateIdle) {
        if (was_awake) {
            dog_.RequestAction(kActionStateGoIdle);
            current_action_ = "sleep";
        }
        if (display) {
            // This runs in the same state transition as Idle, before the
            // application's later default-emotion update can paint two eyes.
            display->ShowDogSleepFace();
        }
        if (new_state == kDeviceStateIdle) {
            StartLockScreenTimer();
        }
    }
}

void DogController::StartLockScreenTimer() {
    if (lock_screen_timer_ != nullptr) {
        ESP_ERROR_CHECK(esp_timer_start_once(lock_screen_timer_, LOCK_SCREEN_IDLE_MS * 1000ULL));
    }
}

void DogController::StopLockScreenTimer() {
    if (lock_screen_timer_ != nullptr && esp_timer_is_active(lock_screen_timer_)) {
        ESP_ERROR_CHECK(esp_timer_stop(lock_screen_timer_));
    }
}

void DogController::OnLockScreenTimer(void* arg) {
    auto controller = static_cast<DogController*>(arg);
    if (controller->startup_complete_ && Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        if (auto display = Board::GetInstance().GetDisplay()) {
            display->ShowLockScreen();
        }
    }
}

// v2.4.0 的 Application/DeviceStateMachine 没有暴露公开的监听器注册接口，
// 为了不改动核心共享文件，这里采用跟旧版本"待机随机动作"一样的轮询方式，
// 监听设备状态的变化（Idle <-> 非Idle），驱动小狗自动站立/睡觉，同时驱动屏幕
// 在"小狗睁眼表情 / 呼呼大睡表情 / 时间锁屏"三种模式之间切换。
void DogController::RegisterStateChangeListenerLegacy() {
    xTaskCreate([](void* arg) {
        auto controller = static_cast<DogController*>(arg);
        auto& app = Application::GetInstance();
        auto display = Board::GetInstance().GetDisplay();
        DeviceState last_state = app.GetDeviceState();
        bool was_awake = IsAwakeState(last_state);
        bool startup_complete = false;
        bool startup_activation_seen = (last_state == kDeviceStateActivating);
        int idle_elapsed_ms = 0;

        // The board is constructed before Application advances to Starting,
        // so the first observed state can be Unknown.  Keep the standard
        // Xiaozhi initialization UI visible until the first successful idle.
        if (display) {
            display->ShowDogStartup();
        }

        while (true) {
            vTaskDelay(200 / portTICK_PERIOD_MS);
            DeviceState state = app.GetDeviceState();
            bool is_awake = IsAwakeState(state);

            if (!startup_complete) {
                if (state == kDeviceStateActivating) {
                    // Activation starts only after the network-connected event.
                    startup_activation_seen = true;
                }
                if (state == kDeviceStateIdle && startup_activation_seen) {
                    startup_complete = true;
                    was_awake = false;
                    idle_elapsed_ms = 0;
                    if (display) display->ShowDogSleepFace();
                } else if (IsStartupState(state) && display) {
                    display->ShowDogStartup();
                }
                last_state = state;
                continue;
            }

            if (state != last_state) {
                if (is_awake) {
                    idle_elapsed_ms = 0;
                    if (!was_awake) {
                        // 从待机被唤醒（用户按键/唤醒词/语音交互开始）：站起来
                        controller->dog_.RequestAction(kActionStateStand);
                    }
                    if (display) {
                        display->ShowDogFace();
                        // 屏幕表情细分：聆听中用合成情绪驱动；说话状态默认回到平静，
                        // 具体情绪交给云端 llm 消息里的 emotion 字段（见 application.cc）
                        if (state == kDeviceStateListening) {
                            display->SetEmotion("listening");
                        } else if (state == kDeviceStateSpeaking) {
                            display->SetEmotion("neutral");
                        }
                    }
                } else if (was_awake) {
                    // 交互结束（比如从 Speaking/Listening 回到 Idle）：先伸懒腰过渡一下，再趴下睡觉
                    // （半闭眼->闭眼的屏幕过渡时机由 DogOledDisplay::ShowDogSleepFace 内部处理）
                    controller->dog_.RequestAction(kActionStateGoIdle);
                    idle_elapsed_ms = 0;
                    if (display) display->ShowDogSleepFace();
                }
                // was_awake 和 is_awake 都是 false（比如 Connecting -> Idle，或 Idle -> WifiConfiguring）：
                // 全程都在"没醒"的范围内，屏幕已经是睡觉表情，不需要重复触发
            } else if (!is_awake && state == kDeviceStateIdle) {
                idle_elapsed_ms += 200;
                if (display && idle_elapsed_ms >= LOCK_SCREEN_IDLE_MS) {
                    display->ShowLockScreen();
                }
            }

            last_state = state;
            was_awake = is_awake;
        }
    }, "dog_state_task", 3072, this, 1, nullptr);
}

void DogController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.web_control.get_url",
        "获取小狗当前真实可访问的局域网网页控制网址。用户询问网页控制地址、控制页面网址、手机如何打开控制台时，必须调用此工具。工具会同时把网址显示在 OLED 屏幕上。",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            const auto url = WebControlServer::GetInstance().GetControlUrl();
            if (url.empty()) {
                return "小狗尚未连接 Wi-Fi，网页控制服务当前不可用。请联网后再问我。";
            }
            if (auto* display = Board::GetInstance().GetDisplay()) {
                display->ShowControlUrl(url.c_str(), 15000);
            }
            return "小狗的网页控制网址是 " + url + "，请让手机连接同一个 Wi-Fi 后打开这个地址。网址也已经显示在屏幕上。";
        });

    mcp_server.AddTool(
        "self.dog.action",
        "控制小狗机器人的动作。action 可选值："
        "walk(向前走)、walk_back(向后退)、stand(站立)、sitdown(坐下)、sleep(趴下睡觉)、"
        "turn_left(左转)、turn_right(右转)、wave(挥手打招呼)、stop(停止当前动作)。",
        PropertyList({
            Property("action", kPropertyTypeString, "stand")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            std::string action = properties["action"].value<std::string>();
            ESP_LOGI(TAG, "dog action: %s", action.c_str());

            if (!ExecuteAction(action)) {
                return "Invalid action. Available actions: walk, walk_back, stand, sitdown, sleep, turn_left, turn_right, wave, stop";
            }
            return true;

            if (action == "walk") {
                dog_.RequestAction(kActionStateWalk);
            } else if (action == "walk_back") {
                dog_.RequestAction(kActionStateWalkBack);
            } else if (action == "stand") {
                dog_.RequestAction(kActionStateStand);
            } else if (action == "sitdown") {
                dog_.RequestAction(kActionStateSitdown);
            } else if (action == "sleep") {
                dog_.RequestAction(kActionStateSleep);
            } else if (action == "turn_left") {
                dog_.RequestAction(kActionStateTurnLeft);
            } else if (action == "turn_right") {
                dog_.RequestAction(kActionStateTurnRight);
            } else if (action == "wave") {
                dog_.RequestAction(kActionStateWave);
            } else if (action == "stop") {
                dog_.RequestAction(kActionStateStop);
            } else {
                return "错误：无效的动作名称。可用动作：walk, walk_back, stand, sitdown, sleep, turn_left, turn_right, wave, stop";
            }
            return true;
        });

    ESP_LOGI(TAG, "小狗动作 MCP 工具注册完成");
}
