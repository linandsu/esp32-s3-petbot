#include "dog_oled_display.h"
#include "sntp_clock.h"

#include <esp_log.h>
#include <cstdlib>
#include <cstdint>
#include <ctime>

#define TAG "DogOledDisplay"

// 屏幕只有 32px 高。之前 EYE_SIZE=40 导致眼睛默认就已经上下顶格裁切，
// "变大"的表情（听/惊讶）跟平静表情裁切后完全一样看不出区别。
// 参考 RoboEyes/esp32-eyes 这类给 128x32 小屏用的 Cozmo 风格眼睛库，
// 基础眼睛控制在屏幕高度以内，留出"还能再变大"的余地。
#define EYE_SIZE 26
#define EYE_BLINK_FREQ 80
#define EYE_GAP 17
#define EYE_RADIUS 6

#define SURPRISED_DURATION_MS 1500

namespace {
struct DelayedTaskContext {
    DogOledDisplay* display;
    uint32_t generation;
};

void SetTranslateY(void* var, int32_t value) {
    lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(var), value, 0);
}

// 把一次长睡眠拆成多段短睡眠，每段都检查一次停止标志——这样标志设下去之后，
// 任务最多 50ms 内就会醒来退出，而不是傻等到这次睡眠的全部时长结束。
// 之前 BlinkTask 一次睡 1~6 秒、EyeMoveTask 睡 3 秒，如果整段不检查标志，
// 外部等待任务退出时基本都会等到超时（见下面 WaitForTaskExit 的注释），
// 表现为每次切换表情都卡顿最多 1.5s。
// 返回 true 表示中途被停止标志唤醒（调用者应立刻退出循环）。
bool SleepChecking(int total_ms, volatile bool& stop_flag) {
    const int kStepMs = 50;
    for (int elapsed = 0; elapsed < total_ms; elapsed += kStepMs) {
        if (stop_flag) return true;
        int step = (total_ms - elapsed) < kStepMs ? (total_ms - elapsed) : kStepMs;
        vTaskDelay(step / portTICK_PERIOD_MS);
    }
    return stop_flag;
}

// 协作式等待任务自行退出：配合上面的 SleepChecking，任务通常几十毫秒内就会
// 主动退出，这里的等待上限只是防御性兜底（覆盖 ToAnyPosition 单次动画的最长
// 耗时），正常情况下不会被真正用到。超时兜底才强制删除，兜底删除仍有极小
// 概率撞上持锁窗口，但比"每次都直接硬杀"概率低几个数量级。
void WaitForTaskExit(TaskHandle_t& handle, volatile bool& stop_flag) {
    if (handle == nullptr) {
        return;
    }
    stop_flag = true;
    for (int i = 0; i < 150 && handle != nullptr; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (handle != nullptr) {
        // 超时兜底：理论上不应该走到这里
        vTaskDelete(handle);
        handle = nullptr;
    }
    stop_flag = false;
}
} // namespace

DogOledDisplay::DogOledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {
    // 之前是在 Blink/EyeMove 任务的每次循环里重新 srand(time(NULL))：同一秒内
    // 多次调用会用同一个种子，导致"随机"眨眼/游走在这 1 秒内退化成完全固定的序列。
    // 改成只在构造时播种一次。
    srand((unsigned int)time(NULL));
}

DogOledDisplay::~DogOledDisplay() {
    StopEyeAnimationTasks();
    StopDriftTask();
    StopClockRefreshTask();
    StopAutoRevertTask();
    StopSleepSway();
    StopZzzAnim();
    if (eye_left_ != nullptr) {
        lv_obj_del(eye_left_);
    }
    if (eye_right_ != nullptr) {
        lv_obj_del(eye_right_);
    }
    if (lock_screen_ != nullptr) {
        lv_obj_del(lock_screen_);
    }
}

void DogOledDisplay::ShowControlUrl(const char* url, int duration_ms) {
    if (url == nullptr || url[0] == '\0') {
        return;
    }

    const uint32_t generation = ++control_url_generation_;
    {
        DisplayLockGuard lock(this);
        if (control_url_label_ == nullptr) {
            control_url_label_ = lv_label_create(lv_screen_active());
            lv_obj_set_size(control_url_label_, width_, height_);
            lv_obj_align(control_url_label_, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_bg_color(control_url_label_, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(control_url_label_, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(control_url_label_, lv_color_white(), 0);
            lv_obj_set_style_text_align(control_url_label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(control_url_label_, 8, 0);
            lv_label_set_long_mode(control_url_label_, LV_LABEL_LONG_WRAP);
        }
        std::string text = "WEB CONTROL\n";
        text += url;
        lv_label_set_text(control_url_label_, text.c_str());
        lv_obj_remove_flag(control_url_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(control_url_label_);
    }

    struct UrlHideContext {
        DogOledDisplay* display;
        uint32_t generation;
        int duration_ms;
    };
    auto* context = new UrlHideContext{this, generation, duration_ms};
    BaseType_t created = xTaskCreate([](void* arg) {
        auto* context = static_cast<UrlHideContext*>(arg);
        auto* display = context->display;
        const auto generation = context->generation;
        const auto duration_ms = context->duration_ms;
        delete context;

        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        if (generation == display->control_url_generation_) {
            DisplayLockGuard lock(display);
            if (display->control_url_label_ != nullptr) {
                lv_obj_add_flag(display->control_url_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (generation == display->control_url_generation_) {
            display->control_url_task_handle_ = nullptr;
        }
        vTaskDelete(nullptr);
    }, "dog_url_hide", 2560, context, 3, &control_url_task_handle_);

    if (created != pdPASS) {
        delete context;
        control_url_task_handle_ = nullptr;
        ESP_LOGE(TAG, "Failed to create control URL display task");
    }
}

void DogOledDisplay::CreateEyes() {
    if (eye_left_ != nullptr) {
        return;
    }
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();

    eye_left_ = lv_obj_create(screen);
    lv_obj_set_size(eye_left_, EYE_SIZE, EYE_SIZE);
    lv_obj_set_style_radius(eye_left_, EYE_RADIUS, 0);
    lv_obj_set_scrollbar_mode(eye_left_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(eye_left_, lv_color_black(), 0);
    lv_obj_set_style_border_width(eye_left_, 0, 0);
    lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_CLICKABLE);

    eye_right_ = lv_obj_create(screen);
    lv_obj_set_size(eye_right_, EYE_SIZE, EYE_SIZE);
    lv_obj_set_style_radius(eye_right_, EYE_RADIUS, 0);
    lv_obj_set_scrollbar_mode(eye_right_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(eye_right_, lv_color_black(), 0);
    lv_obj_set_style_border_width(eye_right_, 0, 0);
    lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_align(eye_left_, LV_ALIGN_CENTER, EYE_GAP, 0);
    lv_obj_align(eye_right_, LV_ALIGN_CENTER, -EYE_GAP, 0);

    cur_eye_left_x_ = EYE_GAP;
    cur_eye_left_y_ = 0;
    cur_eye_right_x_ = -EYE_GAP;
    cur_eye_right_y_ = 0;

    // 睡觉 zzz：固定锚在屏幕右上角，不跟随眼睛位置，避免在矮屏幕上跑出边界
    zzz_label_ = lv_label_create(screen);
    lv_label_set_text(zzz_label_, "z Z z");
    lv_obj_align(zzz_label_, LV_ALIGN_TOP_RIGHT, -2, 0);
    lv_obj_add_flag(zzz_label_, LV_OBJ_FLAG_HIDDEN);

    // 嘴巴：弧线（开心的笑/伤心的哭）
    mouth_arc_ = lv_arc_create(screen);
    lv_obj_set_size(mouth_arc_, 20, 20);
    lv_obj_set_style_width(mouth_arc_, 0, LV_PART_KNOB);
    lv_obj_set_style_height(mouth_arc_, 0, LV_PART_KNOB);
    lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(mouth_arc_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(mouth_arc_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(mouth_arc_, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(mouth_arc_, lv_color_black(), LV_PART_INDICATOR);
    lv_arc_set_bg_angles(mouth_arc_, 0, 360);
    lv_obj_align(mouth_arc_, LV_ALIGN_CENTER, 0, 12);
    lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);

    // 嘴巴：直线（生气/思考）
    mouth_line_ = lv_obj_create(screen);
    lv_obj_set_size(mouth_line_, 14, 2);
    lv_obj_set_style_radius(mouth_line_, 1, 0);
    lv_obj_set_style_bg_color(mouth_line_, lv_color_black(), 0);
    lv_obj_set_style_border_width(mouth_line_, 0, 0);
    lv_obj_set_scrollbar_mode(mouth_line_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(mouth_line_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(mouth_line_, LV_ALIGN_CENTER, 0, 13);
    lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);

    // 嘴巴：小圆（惊讶的 "O" 嘴）
    mouth_circle_ = lv_obj_create(screen);
    lv_obj_set_size(mouth_circle_, 8, 8);
    lv_obj_set_style_radius(mouth_circle_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mouth_circle_, lv_color_black(), 0);
    lv_obj_set_style_border_width(mouth_circle_, 0, 0);
    lv_obj_set_scrollbar_mode(mouth_circle_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(mouth_circle_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(mouth_circle_, LV_ALIGN_CENTER, 0, 12);
    lv_obj_add_flag(mouth_circle_, LV_OBJ_FLAG_HIDDEN);

    // 眼皮遮罩：白色底色形状叠在眼睛上方，"挖掉"眼睛的一角/一部分，
    // 参考 RoboEyes/Cozmo 的做法——用遮罩切出斜挑眉角或圆润笑眼弧线，
    // 而不是整只眼睛旋转（那样只会看起来像歪掉的方块）。
    eyelid_left_ = lv_obj_create(screen);
    lv_obj_set_style_bg_color(eyelid_left_, lv_color_white(), 0);
    lv_obj_set_style_border_width(eyelid_left_, 0, 0);
    lv_obj_set_scrollbar_mode(eyelid_left_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(eyelid_left_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(eyelid_left_, LV_OBJ_FLAG_HIDDEN);

    eyelid_right_ = lv_obj_create(screen);
    lv_obj_set_style_bg_color(eyelid_right_, lv_color_white(), 0);
    lv_obj_set_style_border_width(eyelid_right_, 0, 0);
    lv_obj_set_scrollbar_mode(eyelid_right_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(eyelid_right_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(eyelid_right_, LV_OBJ_FLAG_HIDDEN);

    // 伤心的泪滴
    tear_ = lv_obj_create(screen);
    lv_obj_set_size(tear_, 5, 5);
    lv_obj_set_style_radius(tear_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(tear_, lv_color_black(), 0);
    lv_obj_set_style_border_width(tear_, 0, 0);
    lv_obj_set_scrollbar_mode(tear_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(tear_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(tear_, LV_ALIGN_CENTER, EYE_GAP + EYE_SIZE / 2 + 4, 10);
    lv_obj_add_flag(tear_, LV_OBJ_FLAG_HIDDEN);

    // 惊讶的感叹号（竖条 + 小圆点）
    exclaim_bar_ = lv_obj_create(screen);
    lv_obj_set_size(exclaim_bar_, 2, 10);
    lv_obj_set_style_bg_color(exclaim_bar_, lv_color_black(), 0);
    lv_obj_set_style_border_width(exclaim_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(exclaim_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(exclaim_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(exclaim_bar_, LV_ALIGN_CENTER, 50, -6);
    lv_obj_add_flag(exclaim_bar_, LV_OBJ_FLAG_HIDDEN);

    exclaim_dot_ = lv_obj_create(screen);
    lv_obj_set_size(exclaim_dot_, 2, 2);
    lv_obj_set_style_bg_color(exclaim_dot_, lv_color_black(), 0);
    lv_obj_set_style_border_width(exclaim_dot_, 0, 0);
    lv_obj_set_scrollbar_mode(exclaim_dot_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(exclaim_dot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(exclaim_dot_, LV_ALIGN_CENTER, 50, 2);
    lv_obj_add_flag(exclaim_dot_, LV_OBJ_FLAG_HIDDEN);

    // 尴尬时遮住一只眼睛外侧的爪子
    paw_embarrassed_ = lv_obj_create(screen);
    lv_obj_set_size(paw_embarrassed_, 22, 22);
    lv_obj_set_style_radius(paw_embarrassed_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(paw_embarrassed_, lv_color_black(), 0);
    lv_obj_set_style_border_width(paw_embarrassed_, 0, 0);
    lv_obj_set_scrollbar_mode(paw_embarrassed_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(paw_embarrassed_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(paw_embarrassed_, LV_ALIGN_CENTER, -EYE_GAP, 0);
    lv_obj_add_flag(paw_embarrassed_, LV_OBJ_FLAG_HIDDEN);

    // 思考时摸下巴的手：用折线画一个"打勾 ✓"形状的爪子轮廓，短撇+长捺
    thinking_hand_points_[0] = {0, 4};
    thinking_hand_points_[1] = {4, 9};
    thinking_hand_points_[2] = {13, 0};
    paw_thinking_ = lv_line_create(screen);
    lv_line_set_points(paw_thinking_, thinking_hand_points_, 3);
    lv_obj_set_style_line_width(paw_thinking_, 3, 0);
    lv_obj_set_style_line_color(paw_thinking_, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(paw_thinking_, true, 0);
    lv_obj_set_scrollbar_mode(paw_thinking_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(paw_thinking_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(paw_thinking_, LV_ALIGN_CENTER, 22, 12);
    lv_obj_add_flag(paw_thinking_, LV_OBJ_FLAG_HIDDEN);

    // 思考时戴的单片眼镜：圆框 + 斜挂下垂的一小段挂绳，罩在 eye_right_ 外面
    monocle_ring_ = lv_obj_create(screen);
    lv_obj_set_size(monocle_ring_, EYE_SIZE + 8, EYE_SIZE + 8);
    lv_obj_set_style_radius(monocle_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(monocle_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(monocle_ring_, 2, 0);
    lv_obj_set_style_border_color(monocle_ring_, lv_color_black(), 0);
    lv_obj_set_scrollbar_mode(monocle_ring_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(monocle_ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(monocle_ring_, LV_ALIGN_CENTER, -EYE_GAP, 0);
    lv_obj_add_flag(monocle_ring_, LV_OBJ_FLAG_HIDDEN);

    monocle_chain_ = lv_obj_create(screen);
    lv_obj_set_size(monocle_chain_, 2, 8);
    lv_obj_set_style_radius(monocle_chain_, 1, 0);
    lv_obj_set_style_bg_color(monocle_chain_, lv_color_black(), 0);
    lv_obj_set_style_border_width(monocle_chain_, 0, 0);
    lv_obj_set_scrollbar_mode(monocle_chain_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(monocle_chain_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_transform_pivot_x(monocle_chain_, 1, 0);
    lv_obj_set_style_transform_pivot_y(monocle_chain_, 0, 0);
    lv_obj_set_style_transform_rotation(monocle_chain_, 300, 0);
    lv_obj_align(monocle_chain_, LV_ALIGN_CENTER, -EYE_GAP + EYE_SIZE / 2 + 2, EYE_SIZE / 2 + 2);
    lv_obj_add_flag(monocle_chain_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
}

void DogOledDisplay::CreateLockScreen() {
    if (lock_screen_ != nullptr) {
        return;
    }
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();

    lock_screen_ = lv_obj_create(screen);
    lv_obj_set_size(lock_screen_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(lock_screen_, 0, 0);
    lv_obj_set_style_border_width(lock_screen_, 0, 0);
    lv_obj_set_style_radius(lock_screen_, 0, 0);
    lv_obj_set_scrollbar_mode(lock_screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(lock_screen_, LV_OBJ_FLAG_CLICKABLE);

    // 128x32 的小屏很矮，用"时间贴顶、日期贴底"各自独立锚定，
    // 保证不管字体行高多少，日期/星期都不会被屏幕底部裁掉
    time_label_ = lv_label_create(lock_screen_);
    lv_label_set_text(time_label_, "--:--");
    lv_obj_set_style_text_line_space(time_label_, -3, 0);
    if (height_ == 64) {
        // A 30px numeric font makes the clock the visual anchor of the
        // 128x64 lock screen while leaving a clear band for the date below.
        lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_30, 0);
        lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, 5);
    } else {
        lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, -3);
    }

    date_label_ = lv_label_create(lock_screen_);
    lv_label_set_text(date_label_, "");
    lv_obj_set_style_text_line_space(date_label_, -3, 0);
    lv_obj_align(date_label_, LV_ALIGN_BOTTOM_MID, 0, height_ == 64 ? -4 : 0);

    lv_obj_add_flag(lock_screen_, LV_OBJ_FLAG_HIDDEN);
}

int DogOledDisplay::BresenhamLine(int x1, int y1, int x2, int y2, int ret[]) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int count = 0;
    while (1) {
        ret[count] = x1;
        ret[count + 1] = y1;
        count += 2;
        if (x1 == x2 && y1 == y2) {
            break;
        }
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
    return count;
}

void DogOledDisplay::ToAnyPosition(int left_x, int left_y, int right_x, int right_y) {
    int ret1[100];
    int ret2[100];
    int num = BresenhamLine(cur_eye_left_x_, cur_eye_left_y_, left_x, left_y, ret1);
    BresenhamLine(cur_eye_right_x_, cur_eye_right_y_, right_x, right_y, ret2);

    for (int i = 0; i < num; i += 2) {
        {
            DisplayLockGuard lock(this);
            lv_obj_align(eye_left_, LV_ALIGN_CENTER, ret1[i], ret1[i + 1]);
            lv_obj_align(eye_right_, LV_ALIGN_CENTER, ret2[i], ret2[i + 1]);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    DisplayLockGuard lock(this);
    cur_eye_left_x_ = lv_obj_get_x_aligned(eye_left_);
    cur_eye_left_y_ = lv_obj_get_y_aligned(eye_left_);
    cur_eye_right_x_ = lv_obj_get_x_aligned(eye_right_);
    cur_eye_right_y_ = lv_obj_get_y_aligned(eye_right_);
}

void DogOledDisplay::ApplyEyeShape(int width, int height, int radius, int gap, int x_offset, int y_offset) {
    DisplayLockGuard lock(this);
    ApplyEyeShapeLocked(width, height, radius, gap, x_offset, y_offset);
}

void DogOledDisplay::ApplyEyeShapeLocked(int width, int height, int radius, int gap, int x_offset, int y_offset) {
    lv_obj_set_size(eye_left_, width, height);
    lv_obj_set_size(eye_right_, width, height);
    lv_obj_set_style_radius(eye_left_, radius, 0);
    lv_obj_set_style_radius(eye_right_, radius, 0);

    // 不再整只眼睛旋转（那样只会看起来像歪掉的方块），眼睛始终保持端正，
    // 具体表情靠 大小/位置 + 眼皮遮罩(ApplyEyelidMask) + 嘴巴/图标/爪子 共同表达
    lv_obj_set_style_transform_rotation(eye_left_, 0, 0);
    lv_obj_set_style_transform_rotation(eye_right_, 0, 0);

    lv_obj_align(eye_left_, LV_ALIGN_CENTER, gap + x_offset, y_offset);
    lv_obj_align(eye_right_, LV_ALIGN_CENTER, -gap + x_offset, y_offset);

    cur_eye_left_x_ = gap + x_offset;
    cur_eye_left_y_ = y_offset;
    cur_eye_right_x_ = -gap + x_offset;
    cur_eye_right_y_ = y_offset;
}

void DogOledDisplay::ApplyEyelidMask(lv_obj_t* mask, int width, int height, int radius, int rotation_tenths, int cx, int cy) {
    DisplayLockGuard lock(this);
    ApplyEyelidMaskLocked(mask, width, height, radius, rotation_tenths, cx, cy);
}

void DogOledDisplay::ApplyEyelidMaskLocked(lv_obj_t* mask, int width, int height, int radius,
                                            int rotation_tenths, int cx, int cy) {
    lv_obj_set_size(mask, width, height);
    lv_obj_set_style_radius(mask, radius, 0);
    lv_obj_set_style_transform_pivot_x(mask, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(mask, height / 2, 0);
    lv_obj_set_style_transform_rotation(mask, rotation_tenths, 0);
    lv_obj_align(mask, LV_ALIGN_CENTER, cx, cy);
    lv_obj_remove_flag(mask, LV_OBJ_FLAG_HIDDEN);
}

void DogOledDisplay::HideEyelids() {
    DisplayLockGuard lock(this);
    HideEyelidsLocked();
}

void DogOledDisplay::HideEyelidsLocked() {
    if (eyelid_left_ != nullptr) lv_obj_add_flag(eyelid_left_, LV_OBJ_FLAG_HIDDEN);
    if (eyelid_right_ != nullptr) lv_obj_add_flag(eyelid_right_, LV_OBJ_FLAG_HIDDEN);
}

void DogOledDisplay::HideEmotionExtras() {
    DisplayLockGuard lock(this);
    HideEmotionExtrasLocked();
}

void DogOledDisplay::HideEmotionExtrasLocked() {
    HideEyelidsLocked();
    lv_obj_t* extras[] = {
        mouth_arc_, mouth_line_, mouth_circle_,
        tear_, exclaim_bar_, exclaim_dot_,
        paw_embarrassed_, paw_thinking_,
        monocle_ring_, monocle_chain_,
    };
    for (auto obj : extras) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

DogOledDisplay::EmotionShape DogOledDisplay::MapEmotionToShape(const std::string& emotion) {
    if (emotion == "listening") return EmotionShape::kListening;
    if (emotion == "connecting") return EmotionShape::kConnecting;
    if (emotion == "thinking" || emotion == "confused") return EmotionShape::kThinking;
    if (emotion == "happy" || emotion == "laughing" || emotion == "funny" ||
        emotion == "delicious" || emotion == "confident" || emotion == "cool") {
        return EmotionShape::kHappy;
    }
    if (emotion == "embarrassed") return EmotionShape::kEmbarrassed;
    if (emotion == "sad" || emotion == "crying") return EmotionShape::kSad;
    if (emotion == "angry") return EmotionShape::kAngry;
    if (emotion == "surprised" || emotion == "shocked") return EmotionShape::kSurprised;
    return EmotionShape::kNeutral;
}

void DogOledDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr || mode_ != Mode::kFace) {
        // 睡觉/锁屏状态下不展示情绪表情，回到对话状态后由 ShowDogFace() 重新对齐
        return;
    }
    CreateEyes();
    ApplyEmotionShape(MapEmotionToShape(emotion));
}

void DogOledDisplay::ApplyEmotionShape(EmotionShape shape) {
    emotion_shape_ = shape;

    StopAutoRevertTask();
    // Stop any writer task before taking the display lock for the atomic
    // update.  Waiting for one while holding that lock would deadlock if it
    // was between animation frames and about to acquire the same lock.
    switch (shape) {
        case EmotionShape::kNeutral:
            StopDriftTask();
            break;
        case EmotionShape::kConnecting:
        case EmotionShape::kThinking:
            StopEyeAnimationTasks();
            break;
        default:
            StopEyeAnimationTasks();
            StopDriftTask();
            break;
    }
    // A complete expression is committed while the LVGL mutex is held.  The
    // previous implementation released this lock between hiding accessories,
    // resizing eyes and showing the new accessories, so the OLED could render
    // an unintended in-between expression.
    DisplayLockGuard lock(this);
    HideEmotionExtrasLocked();

    switch (shape) {
        case EmotionShape::kNeutral: {
            ApplyEyeShapeLocked(EYE_SIZE, EYE_SIZE, EYE_RADIUS, EYE_GAP, 0, 0);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            if (blink_task_handle_ == nullptr) {
                xTaskCreate([](void* arg) {
                    auto this_ = (DogOledDisplay*)arg;
                    this_->BlinkTask();
                    vTaskDelete(NULL);
                }, "dog_blink_task", 2560, this, 5, &blink_task_handle_);
            }
            if (eye_move_task_handle_ == nullptr) {
                xTaskCreate([](void* arg) {
                    auto this_ = (DogOledDisplay*)arg;
                    this_->EyeMoveTask();
                    vTaskDelete(NULL);
                }, "dog_eye_move_task", 3096, this, 5, &eye_move_task_handle_);
            }
            break;
        }
        case EmotionShape::kListening: {
            // "在听" = 瞪大双眼、专注不动（不眨眼、不乱看）。
            // 放大的同时必须把眼距也拉开，否则内侧边缘会贴在一起糊成一只眼睛。
            ApplyEyeShapeLocked((int)(EYE_SIZE * 1.25), (int)(EYE_SIZE * 1.25), EYE_RADIUS + 2, EYE_GAP + 9, 0, 0);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        case EmotionShape::kConnecting: {
            // 连接中 = 半闭眼、缓慢左右漂移，像在等待/张望
            ApplyEyeShapeLocked(EYE_SIZE, (int)(EYE_SIZE * 0.55), EYE_RADIUS, EYE_GAP, 0, -1);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            StartDriftTaskIfNeeded();
            break;
        }
        case EmotionShape::kThinking: {
            // 思考 = 一只眼睛戴单片眼镜、另一只眯起来 + 摸下巴的"打勾"手 + 抿嘴，
            // 组合起来才是一看就懂的"在思考"，而不是单纯眯眼睛
            ApplyEyeShapeLocked(EYE_SIZE, (int)(EYE_SIZE * 0.55), EYE_RADIUS, EYE_GAP, 0, -1);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(paw_thinking_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(monocle_ring_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(monocle_chain_, LV_OBJ_FLAG_HIDDEN);
            StartDriftTaskIfNeeded();
            break;
        }
        case EmotionShape::kHappy: {
            // 开心 = 眯眼笑（下眼皮向上盖住大半只眼睛，露出上弯的一小条黑边）+ 笑嘴
            ApplyEyeShapeLocked(EYE_SIZE, EYE_SIZE, EYE_RADIUS, EYE_GAP, 0, -2);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            // 白色圆角遮罩从下方盖住眼睛，露出的上边缘呈现上翘的笑眼弧线
            ApplyEyelidMaskLocked(eyelid_left_, EYE_SIZE + 6, 22, LV_RADIUS_CIRCLE, 0, EYE_GAP, 10);
            ApplyEyelidMaskLocked(eyelid_right_, EYE_SIZE + 6, 22, LV_RADIUS_CIRCLE, 0, -EYE_GAP, 10);
            lv_arc_set_angles(mouth_arc_, 30, 150);
            lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        case EmotionShape::kSad: {
            // 伤心 = 整体眼睛下移变矮 + 上眼皮盖住上方一角（外侧更多），呈下垂状 + 哭嘴 + 泪滴
            ApplyEyeShapeLocked(EYE_SIZE, (int)(EYE_SIZE * 0.5), EYE_RADIUS, EYE_GAP, 0, 4);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            // 遮罩从外上方斜切下来，呈现"眼角下垂"的委屈感（外侧盖得更多）
            ApplyEyelidMaskLocked(eyelid_left_, 18, 10, 4, 200, EYE_GAP + 6, -3);
            ApplyEyelidMaskLocked(eyelid_right_, 18, 10, 4, -200, -EYE_GAP - 6, -3);
            lv_arc_set_angles(mouth_arc_, 210, 330);
            lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(tear_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        case EmotionShape::kEmbarrassed: {
            ApplyEyeShapeLocked(EYE_SIZE, (int)(EYE_SIZE * 0.6), EYE_RADIUS, EYE_GAP, 0, 0);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(paw_embarrassed_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        case EmotionShape::kAngry: {
            // 生气 = 眼睛压扁 + 内侧上角各切一个 45° 斜角遮罩，形成经典的"V"字倒挂眉角 + 抿嘴
            ApplyEyeShapeLocked(EYE_SIZE, (int)(EYE_SIZE * 0.5), 3, EYE_GAP, 0, -2);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
            // eye_left_ 在屏幕右侧，内侧（朝鼻子一侧）是它的左边；eye_right_ 反之。
            // 45° 菱形遮罩咬在眼睛内侧上角，露出的边缘呈现"内低外高"的倒挂眉角
            ApplyEyelidMaskLocked(eyelid_left_, 14, 14, 2, 450, EYE_GAP - EYE_SIZE / 2, -8);
            ApplyEyelidMaskLocked(eyelid_right_, 14, 14, 2, 450, -(EYE_GAP - EYE_SIZE / 2), -8);
            break;
        }
        case EmotionShape::kSurprised: {
            // 惊讶 = 睁到最大的圆眼睛（比平静大很多，圆角拉满）+ O 形嘴 + 感叹号。
            // 同样要把眼距拉开，否则放大后内侧边缘会贴到一起
            ApplyEyeShapeLocked((int)(EYE_SIZE * 1.15), (int)(EYE_SIZE * 1.15), LV_RADIUS_CIRCLE, EYE_GAP + 7, 0, 0);
            lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(mouth_circle_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(exclaim_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(exclaim_dot_, LV_OBJ_FLAG_HIDDEN);
            // 惊讶只是瞬间反应，过一会儿自动回到平静表情
            auto_revert_stop_ = false;
            const uint32_t generation = ++auto_revert_generation_;
            auto* context = new DelayedTaskContext{this, generation};
            xTaskCreate([](void* arg) {
                auto* context = static_cast<DelayedTaskContext*>(arg);
                auto this_ = context->display;
                const uint32_t generation = context->generation;
                delete context;
                vTaskDelay(SURPRISED_DURATION_MS / portTICK_PERIOD_MS);
                if (!this_->auto_revert_stop_ && generation == this_->auto_revert_generation_ &&
                    this_->mode_ == Mode::kFace && this_->emotion_shape_ == EmotionShape::kSurprised) {
                    this_->ApplyEmotionShape(EmotionShape::kNeutral);
                }
                if (generation == this_->auto_revert_generation_) {
                    this_->auto_revert_task_handle_ = nullptr;
                }
                vTaskDelete(NULL);
            }, "dog_surprise_revert", 2560, context, 3, &auto_revert_task_handle_);
            break;
        }
    }
}

void DogOledDisplay::BlinkTask() {
    int rand_val = 0;
    int flag = 0;
    while (1) {
        if (blink_stop_) break;
        rand_val = rand() % 51;
        flag = rand() % 101;
        if (rand_val < 10) rand_val = 1;
        else if (rand_val > 10 && rand_val < 20) rand_val = 2;
        else if (rand_val > 20 && rand_val < 30) rand_val = 3;
        else if (rand_val > 30 && rand_val < 40) rand_val = 4;
        else if (rand_val > 40 && rand_val < 50) rand_val = 5;
        else rand_val = 6;
        if (SleepChecking(rand_val * 1000, blink_stop_)) break;
        if (flag < EYE_BLINK_FREQ) {
            for (int i = EYE_SIZE; i > 10; i--) {
                if (blink_stop_) break;
                {
                    DisplayLockGuard lock(this);
                    lv_obj_set_size(eye_left_, (int)(EYE_SIZE + i * 0.1), i);
                    lv_obj_set_size(eye_right_, (int)(EYE_SIZE + i * 0.1), i);
                }
                vTaskDelay(15 / portTICK_PERIOD_MS);
            }
            for (int i = 10; i < EYE_SIZE; i++) {
                if (blink_stop_) break;
                {
                    DisplayLockGuard lock(this);
                    lv_obj_set_size(eye_left_, EYE_SIZE, i);
                    lv_obj_set_size(eye_right_, EYE_SIZE, i);
                }
                vTaskDelay(15 / portTICK_PERIOD_MS);
            }
        }
    }
    blink_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void DogOledDisplay::EyeMoveTask() {
    int x = 0;
    int y = 0;
    while (1) {
        if (eye_move_stop_) break;
        // The 128x32 panel leaves only three pixels above/below a 26px eye.
        // The former +/-15 range repeatedly drove the eye outside the panel,
        // which looked like a malformed intermediate expression rather than a
        // deliberate glance.
        x = rand() % 17 - 8;
        y = rand() % 7 - 3;
        if (SleepChecking(3000, eye_move_stop_)) break;
        ToAnyPosition(EYE_GAP + x, 0 + y, -EYE_GAP + x, 0 + y);
    }
    eye_move_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void DogOledDisplay::DriftTask() {
    int dir = 1;
    while (1) {
        if (SleepChecking(1500, drift_stop_)) break;
        ToAnyPosition(EYE_GAP + dir * 10, -2, -EYE_GAP + dir * 10, -2);
        dir = -dir;
    }
    drift_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void DogOledDisplay::ClockRefreshTask() {
    while (1) {
        if (clock_refresh_stop_) break;
        {
            DisplayLockGuard lock(this);
            if (time_label_ != nullptr) {
                if (SntpClock::IsTimeValid()) {
                    lv_label_set_text(time_label_, SntpClock::GetTimeString().c_str());
                    lv_label_set_text(date_label_, SntpClock::GetDateString().c_str());
                } else {
                    lv_label_set_text(time_label_, "--:--");
                    lv_label_set_text(date_label_, "正在校时...");
                }
            }
        }
        if (SleepChecking(1000, clock_refresh_stop_)) break;
    }
    clock_refresh_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void DogOledDisplay::StartZzzAnim() {
    if (zzz_anim_running_) {
        return;
    }
    zzz_anim_running_ = true;
    DisplayLockGuard lock(this);
    lv_anim_init(&zzz_anim_);
    lv_anim_set_var(&zzz_anim_, zzz_label_);
    lv_anim_set_exec_cb(&zzz_anim_, SetTranslateY);
    lv_anim_set_values(&zzz_anim_, 0, -4);
    lv_anim_set_time(&zzz_anim_, 600);
    lv_anim_set_playback_time(&zzz_anim_, 600);
    lv_anim_set_repeat_count(&zzz_anim_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&zzz_anim_);
}

void DogOledDisplay::StopZzzAnim() {
    if (!zzz_anim_running_) {
        return;
    }
    zzz_anim_running_ = false;
    DisplayLockGuard lock(this);
    if (zzz_label_ != nullptr) {
        lv_anim_delete(zzz_label_, SetTranslateY);
        lv_obj_set_style_translate_y(zzz_label_, 0, 0);
    }
}

void DogOledDisplay::StartSleepSway() {
    if (sleep_sway_running_ || eye_left_ == nullptr) {
        return;
    }
    sleep_sway_running_ = true;
    DisplayLockGuard lock(this);
    for (auto* animation : {&sleep_eye_left_anim_, &sleep_eye_right_anim_}) {
        lv_anim_init(animation);
        lv_anim_set_var(animation, animation == &sleep_eye_left_anim_ ? eye_left_ : eye_right_);
        lv_anim_set_exec_cb(animation, SetTranslateY);
        lv_anim_set_values(animation, 0, 2);
        lv_anim_set_time(animation, 1200);
        lv_anim_set_playback_time(animation, 1200);
        lv_anim_set_repeat_count(animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(animation);
    }
}

void DogOledDisplay::StopSleepSway() {
    if (!sleep_sway_running_) {
        return;
    }
    sleep_sway_running_ = false;
    DisplayLockGuard lock(this);
    if (eye_left_ != nullptr) {
        lv_anim_delete(eye_left_, SetTranslateY);
        lv_anim_delete(eye_right_, SetTranslateY);
        lv_obj_set_style_translate_y(eye_left_, 0, 0);
        lv_obj_set_style_translate_y(eye_right_, 0, 0);
    }
}

void DogOledDisplay::StopEyeAnimationTasks() {
    WaitForTaskExit(blink_task_handle_, blink_stop_);
    WaitForTaskExit(eye_move_task_handle_, eye_move_stop_);
}

void DogOledDisplay::StopDriftTask() {
    WaitForTaskExit(drift_task_handle_, drift_stop_);
}

void DogOledDisplay::StartDriftTaskIfNeeded() {
    if (drift_task_handle_ == nullptr) {
        xTaskCreate([](void* arg) {
            auto this_ = (DogOledDisplay*)arg;
            this_->DriftTask();
            vTaskDelete(NULL);
        }, "dog_drift_task", 2560, this, 4, &drift_task_handle_);
    }
}

void DogOledDisplay::StopClockRefreshTask() {
    WaitForTaskExit(clock_refresh_task_handle_, clock_refresh_stop_);
}

void DogOledDisplay::StopAutoRevertTask() {
    auto_revert_stop_ = true;
    ++auto_revert_generation_;
}

void DogOledDisplay::ShowDogFace() {
    if (mode_ == Mode::kFace) {
        return;
    }
    mode_ = Mode::kFace;

    CreateEyes();
    StopClockRefreshTask();
    StopSleepSway();
    StopZzzAnim();

    {
        DisplayLockGuard lock(this);
        HideEmotionExtrasLocked();
        lv_obj_add_flag(zzz_label_, LV_OBJ_FLAG_HIDDEN);
        if (lock_screen_ != nullptr) {
            lv_obj_add_flag(lock_screen_, LV_OBJ_FLAG_HIDDEN);
        }
        SetContentVisibleLocked(false);
    }

    // 每次从睡觉/锁屏刚醒来都先回到平静表情，具体情绪由后续 SetEmotion() 驱动
    ApplyEmotionShape(EmotionShape::kNeutral);
}

void DogOledDisplay::ShowDogStartup() {
    if (mode_ == Mode::kStartup) {
        return;
    }
    mode_ = Mode::kStartup;

    StopEyeAnimationTasks();
    StopDriftTask();
    StopClockRefreshTask();
    StopAutoRevertTask();
    StopSleepSway();
    StopZzzAnim();
    {
        DisplayLockGuard lock(this);
        HideEmotionExtrasLocked();
        if (eye_left_ != nullptr) {
            lv_obj_add_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(zzz_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (lock_screen_ != nullptr) {
            lv_obj_add_flag(lock_screen_, LV_OBJ_FLAG_HIDDEN);
        }
        SetContentVisibleLocked(true);
    }
    // Restore the base OledDisplay hierarchy.  Application and WifiBoard now
    // render their normal initialization, scanning and SSID connection text.
}

void DogOledDisplay::ShowDogSleepFace() {
    if (mode_ == Mode::kSleepFace) {
        return;
    }
    mode_ = Mode::kSleepFace;

    CreateEyes();
    StopEyeAnimationTasks();
    StopDriftTask();
    StopClockRefreshTask();
    StopAutoRevertTask();
    StopSleepSway();
    StopZzzAnim();
    {
        DisplayLockGuard lock(this);
        // Hide the old expression, resize the eyes and reveal the sleep page
        // under one lock so no partially-reset two-eye frame is rendered.
        HideEmotionExtrasLocked();
        ApplyEyeShapeLocked(EYE_SIZE, 6, EYE_RADIUS, EYE_GAP, 0, 0);
        lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(zzz_label_, LV_OBJ_FLAG_HIDDEN);
        if (lock_screen_ != nullptr) {
            lv_obj_add_flag(lock_screen_, LV_OBJ_FLAG_HIDDEN);
        }
        SetContentVisibleLocked(false);
    }

    StartZzzAnim();
    StartSleepSway();
}

void DogOledDisplay::ShowLockScreen() {
    if (mode_ == Mode::kLockScreen) {
        return;
    }
    mode_ = Mode::kLockScreen;

    CreateLockScreen();
    StopEyeAnimationTasks();
    StopDriftTask();
    StopAutoRevertTask();
    StopSleepSway();
    StopZzzAnim();

    {
        DisplayLockGuard lock(this);
        HideEmotionExtrasLocked();
        if (eye_left_ != nullptr) {
            lv_obj_add_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(zzz_label_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(lock_screen_, LV_OBJ_FLAG_HIDDEN);
        SetContentVisibleLocked(false);
    }

    if (clock_refresh_task_handle_ == nullptr) {
        xTaskCreate([](void* arg) {
            auto this_ = (DogOledDisplay*)arg;
            this_->ClockRefreshTask();
            vTaskDelete(NULL);
        }, "dog_clock_task", 2560, this, 3, &clock_refresh_task_handle_);
    }
}
