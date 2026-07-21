#include "dog_oled_display.h"

#include <esp_log.h>
#include <cstdlib>
#include <cstdint>
#include <ctime>

#define TAG "DogOledDisplay"

#define EYE_SIZE 40
#define EYE_BLINK_FREQ 80
#define EYE_GAP 25

DogOledDisplay::DogOledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {
}

DogOledDisplay::~DogOledDisplay() {
    if (blink_task_handle_ != nullptr) {
        vTaskDelete(blink_task_handle_);
    }
    if (eye_move_task_handle_ != nullptr) {
        vTaskDelete(eye_move_task_handle_);
    }
    if (eye_left_ != nullptr) {
        lv_obj_del(eye_left_);
    }
    if (eye_right_ != nullptr) {
        lv_obj_del(eye_right_);
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
    lv_obj_set_style_radius(eye_left_, 4, 0);
    lv_obj_set_scrollbar_mode(eye_left_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(eye_left_, lv_color_black(), 0);
    lv_obj_set_style_border_width(eye_left_, 0, 0);

    eye_right_ = lv_obj_create(screen);
    lv_obj_set_size(eye_right_, EYE_SIZE, EYE_SIZE);
    lv_obj_set_style_radius(eye_right_, 4, 0);
    lv_obj_set_scrollbar_mode(eye_right_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(eye_right_, lv_color_black(), 0);
    lv_obj_set_style_border_width(eye_right_, 0, 0);

    lv_obj_align(eye_left_, LV_ALIGN_CENTER, EYE_GAP, 0);
    lv_obj_align(eye_right_, LV_ALIGN_CENTER, -EYE_GAP, 0);

    cur_eye_left_x_ = lv_obj_get_x_aligned(eye_left_);
    cur_eye_left_y_ = lv_obj_get_y_aligned(eye_left_);
    cur_eye_right_x_ = lv_obj_get_x_aligned(eye_right_);
    cur_eye_right_y_ = lv_obj_get_y_aligned(eye_right_);

    lv_obj_add_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
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

void DogOledDisplay::CloseEyes() {
    for (int i = EYE_SIZE; i > 10; i--) {
        {
            DisplayLockGuard lock(this);
            lv_obj_set_size(eye_left_, (int)(EYE_SIZE + i * 0.1), i);
            lv_obj_set_size(eye_right_, (int)(EYE_SIZE + i * 0.1), i);
        }
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}

void DogOledDisplay::OpenEyes() {
    for (int i = 10; i < EYE_SIZE; i++) {
        {
            DisplayLockGuard lock(this);
            lv_obj_set_size(eye_left_, EYE_SIZE, i);
            lv_obj_set_size(eye_right_, EYE_SIZE, i);
        }
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}

void DogOledDisplay::BlinkEyes() {
    CloseEyes();
    OpenEyes();
}

void DogOledDisplay::BlinkTask() {
    int rand_val = 0;
    int flag = 0;
    while (1) {
        srand((unsigned int)time(NULL));
        rand_val = rand() % 51;
        flag = rand() % 101;
        if (rand_val < 10) rand_val = 1;
        else if (rand_val > 10 && rand_val < 20) rand_val = 2;
        else if (rand_val > 20 && rand_val < 30) rand_val = 3;
        else if (rand_val > 30 && rand_val < 40) rand_val = 4;
        else if (rand_val > 40 && rand_val < 50) rand_val = 5;
        else rand_val = 6;
        vTaskDelay(rand_val * 1000 / portTICK_PERIOD_MS);
        if (flag < EYE_BLINK_FREQ) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            BlinkEyes();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void DogOledDisplay::EyeMoveTask() {
    int x = 0;
    int y = 0;
    while (1) {
        srand((unsigned int)time(NULL));
        x = rand() % 31 - 15;
        y = rand() % 31 - 15;
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ToAnyPosition(EYE_GAP + x, 0 + y, -EYE_GAP + x, 0 + y);
    }
}

void DogOledDisplay::StartIdleEmotion() {
    CreateEyes();
    OpenEyes();

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

    {
        DisplayLockGuard lock(this);
        lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
    }
    SetContentVisible(false);
}

void DogOledDisplay::StopIdleEmotion() {
    if (blink_task_handle_ != nullptr) {
        vTaskDelete(blink_task_handle_);
        blink_task_handle_ = nullptr;
    }
    if (eye_move_task_handle_ != nullptr) {
        vTaskDelete(eye_move_task_handle_);
        eye_move_task_handle_ = nullptr;
    }

    if (eye_left_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_obj_add_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
    }
    SetContentVisible(true);
}

void DogOledDisplay::ShowIdleRestEmotion() {
    if (blink_task_handle_ != nullptr) {
        vTaskDelete(blink_task_handle_);
        blink_task_handle_ = nullptr;
    }
    if (eye_move_task_handle_ != nullptr) {
        vTaskDelete(eye_move_task_handle_);
        eye_move_task_handle_ = nullptr;
    }

    if (eye_left_ == nullptr) {
        SetContentVisible(true);
        return;
    }

    {
        DisplayLockGuard lock(this);
        lv_obj_remove_flag(eye_left_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(eye_right_, LV_OBJ_FLAG_HIDDEN);
    }

    ToAnyPosition(EYE_GAP, 0, -EYE_GAP, 0);
    CloseEyes();
}
