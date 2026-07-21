#include "dog_controller.h"

#include "application.h"
#include "device_state.h"
#include "mcp_server.h"
#include "board.h"
#include "display.h"
#include "sntp_clock.h"

#include <esp_log.h>

#define TAG "DogController"

// 待机超过这个时长（毫秒）之后，屏幕从"呼呼大睡表情"切换到"时间锁屏"
#define LOCK_SCREEN_IDLE_MS 30000

DogController::DogController(gpio_num_t servo_io_1, gpio_num_t servo_io_2, gpio_num_t servo_io_3, gpio_num_t servo_io_4) {
    SntpClock::EnsureStarted();

    dog_.InitializeDog(servo_io_1, servo_io_2, servo_io_3, servo_io_4);
    dog_.RequestAction(kActionStateSleep);

    RegisterStateChangeListener();
    RegisterMcpTools();
}

// v2.4.0 的 Application/DeviceStateMachine 没有暴露公开的监听器注册接口，
// 为了不改动核心共享文件，这里采用跟旧版本"待机随机动作"一样的轮询方式，
// 监听设备状态的变化（Idle <-> 非Idle），驱动小狗自动站立/睡觉，同时驱动屏幕
// 在"小狗睁眼表情 / 呼呼大睡表情 / 时间锁屏"三种模式之间切换。
void DogController::RegisterStateChangeListener() {
    xTaskCreate([](void* arg) {
        auto controller = static_cast<DogController*>(arg);
        auto& app = Application::GetInstance();
        auto display = Board::GetInstance().GetDisplay();
        DeviceState last_state = app.GetDeviceState();
        int idle_elapsed_ms = 0;

        // 启动时先按当前状态对齐一次屏幕/动作
        if (last_state == kDeviceStateIdle) {
            if (display) display->ShowDogSleepFace();
        } else if (display) {
            display->ShowDogFace();
        }

        while (true) {
            vTaskDelay(200 / portTICK_PERIOD_MS);
            DeviceState state = app.GetDeviceState();

            if (state != kDeviceStateIdle) {
                idle_elapsed_ms = 0;
                if (last_state == kDeviceStateIdle) {
                    // 从待机被唤醒（用户按键/唤醒词/语音交互开始）：站起来
                    controller->dog_.RequestAction(kActionStateStand);
                }
                if (display) display->ShowDogFace();
            } else {
                if (last_state != kDeviceStateIdle) {
                    // 交互结束刚进入待机：先伸懒腰过渡一下，再趴下睡觉
                    controller->dog_.RequestAction(kActionStateGoIdle);
                    idle_elapsed_ms = 0;
                } else {
                    idle_elapsed_ms += 200;
                }

                if (display) {
                    if (idle_elapsed_ms < LOCK_SCREEN_IDLE_MS) {
                        display->ShowDogSleepFace();
                    } else {
                        display->ShowLockScreen();
                    }
                }
            }

            last_state = state;
        }
    }, "dog_state_task", 3072, this, 1, nullptr);
}

void DogController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

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
