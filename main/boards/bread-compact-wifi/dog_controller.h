#ifndef DOG_CONTROLLER_H
#define DOG_CONTROLLER_H

#include <esp_timer.h>

#include <string>

#include "device_state.h"
#include "pet_dog.h"

// 负责把 PetDog 的动作能力注册为 MCP 工具，并在设备状态变化时驱动小狗的
// 待机/唤醒联动（睡觉/站立），替代旧版里 application.cc 中散落的调用。
class DogController {
public:
    DogController(gpio_num_t servo_io_1, gpio_num_t servo_io_2, gpio_num_t servo_io_3,
                  gpio_num_t servo_io_4, gpio_num_t servo_io_tail);

    static DogController* GetInstance() { return instance_; }
    bool ExecuteAction(const std::string& action);
    bool ExecuteDrive(float forward, float turn);
    std::string GetCurrentAction() const;
    bool IsActionRunning() const { return dog_.IsActionRunning(); }

private:
    PetDog dog_;
    static DogController* instance_;
    std::string current_action_ = "sleep";
    bool startup_complete_ = false;
    bool startup_activation_seen_ = false;
    esp_timer_handle_t lock_screen_timer_ = nullptr;
    esp_timer_handle_t drive_watchdog_timer_ = nullptr;

    void RegisterStateChangeListener();
    void RegisterStateChangeListenerLegacy();
    void HandleDeviceStateChanged(DeviceState old_state, DeviceState new_state);
    void StartLockScreenTimer();
    void StopLockScreenTimer();
    static void OnLockScreenTimer(void* arg);
    static void OnDriveWatchdog(void* arg);
    void ArmDriveWatchdog();
    void StopDriveWatchdog();
    void RegisterMcpTools();
};

#endif // DOG_CONTROLLER_H
