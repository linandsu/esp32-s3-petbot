#ifndef DOG_OLED_DISPLAY_H
#define DOG_OLED_DISPLAY_H

#include "display/oled_display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 在标准 OledDisplay 基础上，把小狗的"脸"做成默认对话页面：
//   - ShowDogFace()：睁眼、眨眼、眼珠游走的正常表情（对话/待机刚开始时）
//   - ShowDogSleepFace()：闭眼 + "Z Z Z" 的呼呼大睡表情（进入待机后）
//   - ShowLockScreen()：待机超过一段时间后的时间/日期锁屏
class DogOledDisplay : public OledDisplay {
public:
    DogOledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, bool mirror_x, bool mirror_y);
    ~DogOledDisplay();

    virtual void ShowDogFace() override;
    virtual void ShowDogSleepFace() override;
    virtual void ShowLockScreen() override;

private:
    enum class Mode {
        kNone,
        kFace,
        kSleepFace,
        kLockScreen,
    };

    lv_obj_t* eye_left_ = nullptr;
    lv_obj_t* eye_right_ = nullptr;
    lv_obj_t* zzz_label_ = nullptr;

    lv_obj_t* lock_screen_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;

    Mode mode_ = Mode::kNone;

    TaskHandle_t blink_task_handle_ = nullptr;
    TaskHandle_t eye_move_task_handle_ = nullptr;
    TaskHandle_t clock_refresh_task_handle_ = nullptr;

    int cur_eye_left_x_ = 0;
    int cur_eye_left_y_ = 0;
    int cur_eye_right_x_ = 0;
    int cur_eye_right_y_ = 0;

    void CreateEyes();
    void CreateLockScreen();

    void StopEyeAnimationTasks();
    void StopClockRefreshTask();

    void ToAnyPosition(int left_x, int left_y, int right_x, int right_y);
    static int BresenhamLine(int x1, int y1, int x2, int y2, int ret[]);

    void SetEyesOpen();
    void SetEyesClosed();

    void BlinkTask();
    void EyeMoveTask();
    void ClockRefreshTask();
};

#endif // DOG_OLED_DISPLAY_H
