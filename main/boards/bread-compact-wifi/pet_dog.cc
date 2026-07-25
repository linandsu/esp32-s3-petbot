#include "pet_dog.h"

#include <algorithm>
#include <cmath>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"

#define TAG "PetDog"

float PetDog::ClampUnit(float value) {
    if (value < -1.f) return -1.f;
    if (value > 1.f) return 1.f;
    return value;
}

int PetDog::ScaleTowardStand(int angle, int stand, float scale) {
    const float clamped = std::max(0.2f, std::min(1.6f, scale));
    return stand + static_cast<int>(std::lround((angle - stand) * clamped));
}

PetDog::PetDog() {
    ledc_timer_ = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer_));
}

PetDog::~PetDog() {
    StopPwm();
}

void PetDog::InitializeDog(gpio_num_t servo_io_1, gpio_num_t servo_io_2,
                           gpio_num_t servo_io_3, gpio_num_t servo_io_4,
                           gpio_num_t servo_io_tail) {
    action_request_event_ = xEventGroupCreate();
    const ledc_channel_config_t channels[] = {
        {.gpio_num = servo_io_1, .speed_mode = LEDC_MODE, .channel = CHANNEL_0, .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = servo_io_2, .speed_mode = LEDC_MODE, .channel = CHANNEL_1, .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = servo_io_3, .speed_mode = LEDC_MODE, .channel = CHANNEL_2, .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = servo_io_4, .speed_mode = LEDC_MODE, .channel = CHANNEL_3, .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0},
        {.gpio_num = servo_io_tail, .speed_mode = LEDC_MODE, .channel = CHANNEL_TAIL, .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0},
    };
    for (const auto& channel : channels) ESP_ERROR_CHECK(ledc_channel_config(&channel));
    set_tail_angle(TAIL_CENTER_ANGLE);

    xTaskCreate([](void* arg) {
        static_cast<PetDog*>(arg)->ActionTask();
        vTaskDelete(nullptr);
    }, "dog_action", 4096, this, 2, nullptr);
}

void PetDog::RequestAction(ActionState state) {
    ESP_LOGI(TAG, "RequestAction state=%d prev_gen=%u action_running=%d", (int)state,
             (unsigned)request_generation_, (int)action_running_);
    // Discrete actions cancel continuous drive.
    drive_forward_ = 0.f;
    drive_turn_ = 0.f;
    // A request replaces, rather than queues behind, the running action.
    if (action_running_ && state != kActionStateStop) settle_before_next_ = true;
    // Pending work counts as running as well, so a second double-tap cannot
    // sneak in before ActionTask has picked up the first request.
    action_running_ = true;
    requested_state_ = state;
    ++request_generation_;
    xEventGroupSetBits(action_request_event_, kActionRequestEvent);
}

void PetDog::SetDrive(float forward, float turn) {
    forward = ClampUnit(forward);
    turn = ClampUnit(turn);
    const bool idle = std::fabs(forward) < kDriveDeadzone && std::fabs(turn) < kDriveDeadzone;
    if (idle) {
        drive_forward_ = 0.f;
        drive_turn_ = 0.f;
        if (!drive_active_) return;
        // Drive loop will see zeros, stand, and exit without a generation bump.
        return;
    }

    drive_forward_ = forward;
    drive_turn_ = turn;
    // Already in / starting Drive: only refresh live setpoints.
    if (drive_active_ || (action_running_ && requested_state_ == kActionStateDrive)) {
        return;
    }

    ESP_LOGI(TAG, "SetDrive start f=%.2f t=%.2f", forward, turn);
    if (action_running_) settle_before_next_ = true;
    action_running_ = true;
    requested_state_ = kActionStateDrive;
    ++request_generation_;
    xEventGroupSetBits(action_request_event_, kActionRequestEvent);
}

void PetDog::ActionTask() {
    while (true) {
        xEventGroupWaitBits(action_request_event_, kActionRequestEvent, pdTRUE, pdTRUE, portMAX_DELAY);
        const ActionState state = requested_state_;
        const uint32_t generation = request_generation_;
        const bool settle_first = settle_before_next_;
        settle_before_next_ = false;
        active_generation_ = generation;
        executing_state_ = state;
        action_running_ = true;
        drive_active_ = (state == kActionStateDrive);
        RunAction(state, generation, settle_first);
        if (state == kActionStateDrive) drive_active_ = false;
        if (!IsCancelled(generation)) {
            action_running_ = false;
            if (state == kActionStateWave || state == kActionStateStop || state == kActionStateDrive) {
                executing_state_ = kActionStateStand;
            } else if (state == kActionStateGoIdle) {
                executing_state_ = kActionStateSleep;
            }
        }
    }
}

bool PetDog::IsCancelled(uint32_t generation) const {
    return generation != request_generation_;
}

bool PetDog::DelayCancelable(uint32_t milliseconds, uint32_t generation) {
    constexpr uint32_t kSliceMs = 20;
    for (uint32_t elapsed = 0; elapsed < milliseconds; elapsed += kSliceMs) {
        if (IsCancelled(generation)) return false;
        const uint32_t slice = std::min(kSliceMs, milliseconds - elapsed);
        vTaskDelay(pdMS_TO_TICKS(slice));
    }
    return !IsCancelled(generation);
}

bool PetDog::MoveToAngles(int lf, int rf, int lb, int rb, uint32_t generation, int step_delay_ms) {
    while (left_front_angle_ != lf || right_front_angle_ != rf ||
           left_back_angle_ != lb || right_back_angle_ != rb) {
        if (IsCancelled(generation)) return false;
        if (left_front_angle_ < lf) set_left_front_angle(left_front_angle_ + 1);
        else if (left_front_angle_ > lf) set_left_front_angle(left_front_angle_ - 1);
        if (right_front_angle_ < rf) set_right_front_angle(right_front_angle_ + 1);
        else if (right_front_angle_ > rf) set_right_front_angle(right_front_angle_ - 1);
        if (left_back_angle_ < lb) set_left_back_angle(left_back_angle_ + 1);
        else if (left_back_angle_ > lb) set_left_back_angle(left_back_angle_ - 1);
        if (right_back_angle_ < rb) set_right_back_angle(right_back_angle_ + 1);
        else if (right_back_angle_ > rb) set_right_back_angle(right_back_angle_ - 1);
        if (!DelayCancelable(step_delay_ms, generation)) return false;
    }
    return true;
}

bool PetDog::SetPoseDirect(int lf, int rf, int lb, int rb, uint32_t generation, uint32_t step_delay_ms) {
    if (IsCancelled(generation)) return false;
    set_left_front_angle(lf);
    if (!DelayCancelable(step_delay_ms, generation)) return false;
    set_right_front_angle(rf);
    if (!DelayCancelable(step_delay_ms, generation)) return false;
    set_left_back_angle(lb);
    if (!DelayCancelable(step_delay_ms, generation)) return false;
    set_right_back_angle(rb);
    return DelayCancelable(step_delay_ms, generation);
}

bool PetDog::Stand(uint32_t generation) {
    set_tail_angle(TAIL_CENTER_ANGLE);
    return MoveToAngles(STAND_ANGLE_LF, STAND_ANGLE_RF, STAND_ANGLE_LB, STAND_ANGLE_RB, generation);
}

bool PetDog::Sitdown(uint32_t generation) {
    return MoveToAngles(SITDOWN_ANGLE_LF, SITDOWN_ANGLE_RF, SITDOWN_ANGLE_LB, SITDOWN_ANGLE_RB, generation);
}

bool PetDog::Sleep(uint32_t generation) {
    ESP_LOGI(TAG, "Sleep begin gen=%u start(lf=%d rf=%d lb=%d rb=%d) target(lf=%d rf=%d lb=%d rb=%d)",
             (unsigned)generation, left_front_angle_, right_front_angle_, left_back_angle_, right_back_angle_,
             SLEEP_ANGLE_LF, SLEEP_ANGLE_RF, SLEEP_ANGLE_LB, SLEEP_ANGLE_RB);
    const bool moved = MoveToAngles(SLEEP_ANGLE_LF, SLEEP_ANGLE_RF, SLEEP_ANGLE_LB, SLEEP_ANGLE_RB, generation, 8);
    ESP_LOGI(TAG, "Sleep move %s gen=%u end(lf=%d rf=%d lb=%d rb=%d) cur_gen=%u",
             moved ? "DONE" : "CANCELLED", (unsigned)generation,
             left_front_angle_, right_front_angle_, left_back_angle_, right_back_angle_,
             (unsigned)request_generation_);
    if (!moved) return false;
    if (!DelayCancelable(2000, generation)) return false;
    StopPwm();
    return true;
}

bool PetDog::StretchThenSleep(uint32_t generation) {
    // Original stretch pace (20ms/degree ease), but no static holds between
    // poses: the eased movement itself is the only timing, so the sequence
    // flows straight through without pausing in the middle.
    // Ends back at Stand before Sleep (matching the original firmware),
    // so the final move into the sleep posture folds the limbs in from a
    // normal standing pose instead of from an extreme stretch extension.
    if (!Stand(generation)) return false;
    if (!MoveToAngles(10, 10, 45, 45, generation, 20)) return false;
    if (!MoveToAngles(135, 135, 170, 170, generation, 20)) return false;
    if (!Stand(generation)) return false;
    return Sleep(generation);
}

bool PetDog::Wave(uint32_t generation) {
    if (!MoveToAngles(90, 90, 50, 0, generation) || !DelayCancelable(600, generation)) return false;
    for (int wave = 0; wave < 5; ++wave) {
        for (int angle = 0; angle <= 64; angle += 4) {
            if (IsCancelled(generation)) return false;
            set_left_front_angle(angle);
            if (!DelayCancelable(25, generation)) return false;
        }
        for (int angle = 64; angle > 0; angle -= 4) {
            if (IsCancelled(generation)) return false;
            set_left_front_angle(angle);
            if (!DelayCancelable(25, generation)) return false;
        }
    }
    return DelayCancelable(1000, generation) && Stand(generation);
}

bool PetDog::WagTail(uint32_t generation) {
    // Tail-only motion: do not force the body into stand.
    set_tail_angle(TAIL_CENTER_ANGLE);
    if (!DelayCancelable(120, generation)) return false;

    for (int cycle = 0; cycle < 6; ++cycle) {
        for (int angle = TAIL_CENTER_ANGLE; angle <= TAIL_RIGHT_ANGLE; angle += 5) {
            if (IsCancelled(generation)) return false;
            set_tail_angle(angle);
            if (!DelayCancelable(18, generation)) return false;
        }
        for (int angle = TAIL_RIGHT_ANGLE; angle >= TAIL_LEFT_ANGLE; angle -= 5) {
            if (IsCancelled(generation)) return false;
            set_tail_angle(angle);
            if (!DelayCancelable(18, generation)) return false;
        }
        for (int angle = TAIL_LEFT_ANGLE; angle <= TAIL_CENTER_ANGLE; angle += 5) {
            if (IsCancelled(generation)) return false;
            set_tail_angle(angle);
            if (!DelayCancelable(18, generation)) return false;
        }
    }

    set_tail_angle(TAIL_CENTER_ANGLE);
    return DelayCancelable(200, generation);
}

bool PetDog::Walk(bool backwards, uint32_t generation) {
    static constexpr int forward[][4] = {{90,45,45,90},{135,45,45,135},{135,90,90,135},{90,90,90,90},{45,90,90,45},{45,135,135,45},{90,135,135,90},{90,90,90,90}};
    static constexpr int backward[][4] = {{90,90,90,90},{90,135,135,90},{45,135,135,45},{45,90,90,45},{90,90,90,90},{135,90,90,135},{135,45,45,135},{90,45,45,90}};
    const int (*pattern)[4] = backwards ? backward : forward;
    while (!IsCancelled(generation)) {
        for (int index = 0; index < 8; ++index) {
            const auto& pose = pattern[index];
            // Direct pose jump (matches original walkfront/walkBack timing),
            // not a per-degree ease: keeps the gait at its original cadence.
            if (!SetPoseDirect(pose[0], pose[1], pose[2], pose[3], generation, 40)) return false;
        }
    }
    return false;
}

bool PetDog::Turn(bool right, uint32_t generation) {
    static constexpr int left[][4] = {{90,90,90,90},{130,90,90,50},{130,130,50,50},{90,130,50,90}};
    static constexpr int right_pattern[][4] = {{90,130,90,50},{130,130,50,50},{130,90,90,50},{90,90,90,90}};
    const int (*pattern)[4] = right ? right_pattern : left;
    while (!IsCancelled(generation)) {
        for (int index = 0; index < 4; ++index) {
            const auto& pose = pattern[index];
            // Direct pose jump (matches original turnLeft/turnRight timing:
            // ~40ms per leg, ~160ms per pose), not a slow per-degree ease.
            if (!SetPoseDirect(pose[0], pose[1], pose[2], pose[3], generation, 40)) return false;
        }
    }
    return false;
}

bool PetDog::Drive(uint32_t generation) {
    static constexpr int forward[][4] = {
        {90,45,45,90},{135,45,45,135},{135,90,90,135},{90,90,90,90},
        {45,90,90,45},{45,135,135,45},{90,135,135,90},{90,90,90,90}};
    static constexpr int backward[][4] = {
        {90,90,90,90},{90,135,135,90},{45,135,135,45},{45,90,90,45},
        {90,90,90,90},{135,90,90,135},{135,45,45,135},{90,45,45,90}};
    static constexpr int turn_left[][4] = {{90,90,90,90},{130,90,90,50},{130,130,50,50},{90,130,50,90}};
    static constexpr int turn_right[][4] = {{90,130,90,50},{130,130,50,50},{130,90,90,50},{90,90,90,90}};

    while (!IsCancelled(generation)) {
        const float f = drive_forward_;
        const float t = drive_turn_;
        if (std::fabs(f) < kDriveDeadzone && std::fabs(t) < kDriveDeadzone) {
            Stand(generation);
            return true;
        }

        // Pure yaw / near-stationary: reuse discrete turn cycle once, then re-sample.
        if (std::fabs(f) < kDriveSpinThreshold) {
            const int (*pattern)[4] = (t >= 0.f) ? turn_right : turn_left;
            for (int index = 0; index < 4; ++index) {
                if (IsCancelled(generation)) return false;
                if (std::fabs(drive_forward_) >= kDriveSpinThreshold ||
                    (std::fabs(drive_forward_) < kDriveDeadzone &&
                     std::fabs(drive_turn_) < kDriveDeadzone)) {
                    break;
                }
                const auto& pose = pattern[index];
                if (!SetPoseDirect(pose[0], pose[1], pose[2], pose[3], generation, kDrivePoseDelayMs)) {
                    return false;
                }
            }
            continue;
        }

        // Travel with asymmetric left/right scale => arc (e.g. forward-left).
        // turn>0 right: left legs take larger steps; turn<0 left: right legs larger.
        const int (*pattern)[4] = (f < 0.f) ? backward : forward;
        for (int index = 0; index < 8; ++index) {
            if (IsCancelled(generation)) return false;
            if (std::fabs(drive_forward_) < kDriveDeadzone &&
                std::fabs(drive_turn_) < kDriveDeadzone) {
                break;
            }
            // If operator released travel for a hard spin, leave this cycle early.
            if (std::fabs(drive_forward_) < kDriveSpinThreshold) break;

            const float live_t = drive_turn_;
            const float live_left = 1.f + live_t * kDriveTurnBias;
            const float live_right = 1.f - live_t * kDriveTurnBias;
            const auto& pose = pattern[index];
            const int lf = ScaleTowardStand(pose[0], STAND_ANGLE_LF, live_left);
            const int rf = ScaleTowardStand(pose[1], STAND_ANGLE_RF, live_right);
            const int lb = ScaleTowardStand(pose[2], STAND_ANGLE_LB, live_left);
            const int rb = ScaleTowardStand(pose[3], STAND_ANGLE_RB, live_right);
            if (!SetPoseDirect(lf, rf, lb, rb, generation, kDrivePoseDelayMs)) return false;
        }
    }
    return false;
}

void PetDog::RunAction(ActionState state, uint32_t generation, bool settle_first) {
    if (state == kActionStateStop) {
        Stand(generation);
        return;
    }
    // Tail wag is independent of body pose — never force a stand settle.
    if (settle_first && state != kActionStateWagTail && !Stand(generation)) return;
    switch (state) {
        case kActionStateWalk: Walk(false, generation); break;
        case kActionStateWalkBack: Walk(true, generation); break;
        case kActionStateTurnLeft: Turn(false, generation); break;
        case kActionStateTurnRight: Turn(true, generation); break;
        case kActionStateSleep: Sleep(generation); break;
        case kActionStateGoIdle: StretchThenSleep(generation); break;
        case kActionStateStand: Stand(generation); break;
        case kActionStateSitdown: Sitdown(generation); break;
        case kActionStateWave: Wave(generation); break;
        case kActionStateWagTail: WagTail(generation); break;
        case kActionStateDrive: Drive(generation); break;
        case kActionStateStop: break;
    }
}

void PetDog::StopPwm() {
    ledc_stop(LEDC_MODE, CHANNEL_0, 0);
    ledc_stop(LEDC_MODE, CHANNEL_1, 0);
    ledc_stop(LEDC_MODE, CHANNEL_2, 0);
    ledc_stop(LEDC_MODE, CHANNEL_3, 0);
    ledc_stop(LEDC_MODE, CHANNEL_TAIL, 0);
}

void PetDog::set_right_back_angle(int angle) {
    right_back_angle_ = angle;
    ledc_set_duty(LEDC_MODE, CHANNEL_0, (180 - angle) * per_angle + LEDC_MIN_DUTY);
    ledc_update_duty(LEDC_MODE, CHANNEL_0);
}

void PetDog::set_left_front_angle(int angle) {
    left_front_angle_ = angle;
    ledc_set_duty(LEDC_MODE, CHANNEL_1, angle * per_angle + LEDC_MIN_DUTY);
    ledc_update_duty(LEDC_MODE, CHANNEL_1);
}

void PetDog::set_right_front_angle(int angle) {
    right_front_angle_ = angle;
    ledc_set_duty(LEDC_MODE, CHANNEL_2, (180 - angle) * per_angle + LEDC_MIN_DUTY);
    ledc_update_duty(LEDC_MODE, CHANNEL_2);
}

void PetDog::set_left_back_angle(int angle) {
    left_back_angle_ = angle;
    ledc_set_duty(LEDC_MODE, CHANNEL_3, angle * per_angle + LEDC_MIN_DUTY);
    ledc_update_duty(LEDC_MODE, CHANNEL_3);
}

void PetDog::set_tail_angle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    tail_angle_ = angle;
    ledc_set_duty(LEDC_MODE, CHANNEL_TAIL, angle * per_angle + LEDC_MIN_DUTY);
    ledc_update_duty(LEDC_MODE, CHANNEL_TAIL);
}
