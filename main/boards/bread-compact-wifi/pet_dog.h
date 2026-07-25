#ifndef PET_DOG_H
#define PET_DOG_H

#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define STAND_ANGLE_LF 90
#define STAND_ANGLE_RF 103
#define STAND_ANGLE_LB 87
#define STAND_ANGLE_RB 90

// LF and RB visually barely move at their "matching" extreme (180/0) even
// though the PWM command is confirmed correct in logs - likely a servo-horn
// spline assembly offset makes their usable range asymmetric. Trying the
// opposite extreme for just these two legs to see if that's the direction
// with real mechanical travel on this physical unit.
#define SLEEP_ANGLE_LF 0
#define SLEEP_ANGLE_RF 180
#define SLEEP_ANGLE_LB 0
#define SLEEP_ANGLE_RB 180

#define SITDOWN_ANGLE_LF 90
#define SITDOWN_ANGLE_RF 100
#define SITDOWN_ANGLE_LB 25
#define SITDOWN_ANGLE_RB 25

#define LEDC_TIMER LEDC_TIMER_1
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY 50
#define LEDC_MIN_DUTY (8191 * 0.025)
#define LEDC_MAX_DUTY (8191 * 0.13)
#define per_angle (((LEDC_MAX_DUTY) - (LEDC_MIN_DUTY)) / 180)

#define CHANNEL_0 LEDC_CHANNEL_0
#define CHANNEL_1 LEDC_CHANNEL_1
#define CHANNEL_2 LEDC_CHANNEL_2
#define CHANNEL_3 LEDC_CHANNEL_3
#define CHANNEL_TAIL LEDC_CHANNEL_4

#define TAIL_CENTER_ANGLE 90
#define TAIL_LEFT_ANGLE 55
#define TAIL_RIGHT_ANGLE 125

enum ActionState {
    kActionStateWalk,
    kActionStateSleep,
    kActionStateStand,
    kActionStateSitdown,
    kActionStateWalkBack,
    kActionStateTurnLeft,
    kActionStateTurnRight,
    kActionStateWave,
    kActionStateWagTail,
    kActionStateStop,
    kActionStateGoIdle,
    kActionStateDrive,
};

// One ActionTask owns all servo writes.  A newer request advances the generation,
// causing the old action to leave at its next short delay/servo step.
// Drive mode hot-updates forward/turn without bumping generation so steering
// stays continuous (no stand settle between direction changes).
class PetDog {
public:
    PetDog();
    ~PetDog();
    void InitializeDog(gpio_num_t servo_io_1, gpio_num_t servo_io_2,
                       gpio_num_t servo_io_3, gpio_num_t servo_io_4,
                       gpio_num_t servo_io_tail);
    void RequestAction(ActionState state);
    // Continuous remote drive. forward/turn in [-1,1]. Near-zero stops Drive.
    void SetDrive(float forward, float turn);
    ActionState GetActionState() const { return executing_state_; }
    bool IsActionRunning() const { return action_running_; }
    bool IsDriving() const { return drive_active_; }

private:
    static constexpr EventBits_t kActionRequestEvent = BIT0;
    // Tunables for continuous drive (ease of on-device iteration).
    static constexpr float kDriveDeadzone = 0.12f;
    static constexpr float kDriveTurnBias = 0.55f;
    static constexpr float kDriveSpinThreshold = 0.22f;
    static constexpr int kDrivePoseDelayMs = 40;

    EventGroupHandle_t action_request_event_ = nullptr;
    ledc_timer_config_t ledc_timer_{};
    volatile ActionState requested_state_ = kActionStateSleep;
    volatile ActionState executing_state_ = kActionStateSleep;
    volatile uint32_t request_generation_ = 0;
    volatile uint32_t active_generation_ = 0;
    volatile bool action_running_ = false;
    volatile bool settle_before_next_ = false;
    volatile bool drive_active_ = false;
    volatile float drive_forward_ = 0.f;
    volatile float drive_turn_ = 0.f;

    int left_front_angle_ = SLEEP_ANGLE_LF - 5;
    int right_front_angle_ = SLEEP_ANGLE_RF - 5;
    int left_back_angle_ = SLEEP_ANGLE_LB + 5;
    int right_back_angle_ = SLEEP_ANGLE_RB + 5;
    int tail_angle_ = TAIL_CENTER_ANGLE;

    void ActionTask();
    void RunAction(ActionState state, uint32_t generation, bool settle_first);
    bool IsCancelled(uint32_t generation) const;
    bool DelayCancelable(uint32_t milliseconds, uint32_t generation);
    bool MoveToAngles(int lf, int rf, int lb, int rb, uint32_t generation, int step_delay_ms = 10);
    // Jumps each leg directly to its target angle (no per-degree easing),
    // pausing step_delay_ms between legs. Mirrors the original gait timing
    // used by walk/turn: ~160ms per full 4-leg pose instead of a slow
    // synchronized ease that stretched a gait cycle to several seconds.
    bool SetPoseDirect(int lf, int rf, int lb, int rb, uint32_t generation, uint32_t step_delay_ms = 40);
    bool Stand(uint32_t generation);
    bool Sitdown(uint32_t generation);
    bool Sleep(uint32_t generation);
    bool StretchThenSleep(uint32_t generation);
    bool Wave(uint32_t generation);
    bool WagTail(uint32_t generation);
    bool Walk(bool backwards, uint32_t generation);
    bool Turn(bool right, uint32_t generation);
    bool Drive(uint32_t generation);
    static float ClampUnit(float value);
    static int ScaleTowardStand(int angle, int stand, float scale);

    void set_left_front_angle(int angle);
    void set_right_front_angle(int angle);
    void set_left_back_angle(int angle);
    void set_right_back_angle(int angle);
    void set_tail_angle(int angle);
    void StopPwm();
};

#endif // PET_DOG_H
