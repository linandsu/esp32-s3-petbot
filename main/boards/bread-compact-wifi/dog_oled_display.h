#ifndef DOG_OLED_DISPLAY_H
#define DOG_OLED_DISPLAY_H

#include "display/oled_display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 在标准 OledDisplay 基础上叠加一套“眼睛”动画，
// 从 0.9.9 版本的 Ssd1306Display 迁移而来，供 PetDog 待机/休眠时使用。
class DogOledDisplay : public OledDisplay {
public:
    DogOledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, bool mirror_x, bool mirror_y);
    ~DogOledDisplay();

    // 开始显示可爱的“眼睛”动画（会短暂隐藏标准 UI）
    virtual void StartIdleEmotion() override;
    // 立即恢复标准 UI，停止眼睛动画
    virtual void StopIdleEmotion() override;
    // 眼睛动画结束后，回正、闭眼、停止动画并恢复标准 UI（用于配合 petsleep）
    virtual void ShowIdleRestEmotion() override;

private:
    lv_obj_t* eye_left_ = nullptr;
    lv_obj_t* eye_right_ = nullptr;

    TaskHandle_t blink_task_handle_ = nullptr;
    TaskHandle_t eye_move_task_handle_ = nullptr;

    int cur_eye_left_x_ = 0;
    int cur_eye_left_y_ = 0;
    int cur_eye_right_x_ = 0;
    int cur_eye_right_y_ = 0;

    void CreateEyes();
    void ToAnyPosition(int left_x, int left_y, int right_x, int right_y);
    static int BresenhamLine(int x1, int y1, int x2, int y2, int ret[]);

    void CloseEyes();
    void OpenEyes();
    void BlinkEyes();

    void BlinkTask();
    void EyeMoveTask();
};

#endif // DOG_OLED_DISPLAY_H
