#ifndef DOG_OLED_DISPLAY_H
#define DOG_OLED_DISPLAY_H

#include "display/oled_display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string>

// 在标准 OledDisplay 基础上，把小狗的"脸"做成默认对话页面。
// 三大页面模式（同一时刻只有一个生效）：
//   - ShowDogFace()：对话状态下的默认页面，内部再细分多种"表情子状态"
//   - ShowDogStartup()：开机联网期间显示标准小智初始化/联网界面
//   - ShowDogSleepFace()：待机时直接闭眼并显示呼呼大睡表情
//   - ShowLockScreen()：待机超过一段时间后的时间/日期锁屏
// ShowDogFace 模式下，具体表情由 SetEmotion() 驱动：
//   - 云端 llm 消息的情绪字段（happy/sad/angry/surprised/thinking/embarrassed...）
//   - DogController 在设备状态边沿上派发的"合成情绪"（listening/connecting/neutral）
class DogOledDisplay : public OledDisplay {
public:
    DogOledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, bool mirror_x, bool mirror_y);
    ~DogOledDisplay();

    virtual void ShowDogFace() override;
    virtual void ShowDogStartup() override;
    virtual void ShowDogSleepFace() override;
    virtual void ShowLockScreen() override;
    virtual void ShowControlUrl(const char* url, int duration_ms = 15000) override;
    virtual void SetEmotion(const char* emotion) override;

private:
    enum class Mode {
        kNone,
        kStartup,
        kFace,
        kSleepFace,
        kLockScreen,
    };

    // 对话页面下的表情子状态，只在 Mode::kFace 时生效
    enum class EmotionShape {
        kNeutral,
        kListening,
        kConnecting,
        kThinking,
        kHappy,
        kSad,
        kEmbarrassed,
        kAngry,
        kSurprised,
    };

    // ---- 眼睛 ----
    lv_obj_t* eye_left_ = nullptr;
    lv_obj_t* eye_right_ = nullptr;

    // ---- 睡觉 zzz ----
    lv_obj_t* zzz_label_ = nullptr;
    lv_anim_t zzz_anim_{};
    bool zzz_anim_running_ = false;
    lv_anim_t sleep_eye_left_anim_{};
    lv_anim_t sleep_eye_right_anim_{};
    bool sleep_sway_running_ = false;

    // ---- 嘴巴（不同表情复用/切换形状） ----
    lv_obj_t* mouth_arc_ = nullptr;    // 开心/伤心的弧线嘴（笑/哭）
    lv_obj_t* mouth_line_ = nullptr;   // 生气/思考的直线嘴
    lv_obj_t* mouth_circle_ = nullptr; // 惊讶的 "O" 形嘴

    // ---- 眼皮遮罩（参考 RoboEyes/Cozmo 风格：用一块底色形状盖住眼睛的一角/一部分，
    //      "切" 出斜挑眉角/圆润笑眼的轮廓，而不是整只眼睛旋转） ----
    lv_obj_t* eyelid_left_ = nullptr;
    lv_obj_t* eyelid_right_ = nullptr;

    // ---- 几何小图标 ----
    lv_obj_t* tear_ = nullptr;         // 伤心的泪滴
    lv_obj_t* exclaim_bar_ = nullptr;  // 惊讶的感叹号
    lv_obj_t* exclaim_dot_ = nullptr;

    // ---- 爪子/手部 ----
    lv_obj_t* paw_embarrassed_ = nullptr; // 尴尬时遮住一只眼睛
    lv_obj_t* paw_thinking_ = nullptr;    // 思考时摸下巴的"打勾"形状手（lv_line 折线）
    lv_point_precise_t thinking_hand_points_[3] = {};

    // ---- 思考时的单片眼镜（罩在一只眼睛外面的圆框 + 挂绳） ----
    lv_obj_t* monocle_ring_ = nullptr;
    lv_obj_t* monocle_chain_ = nullptr;

    // ---- 锁屏 ----
    lv_obj_t* lock_screen_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* control_url_label_ = nullptr;

    Mode mode_ = Mode::kNone;
    EmotionShape emotion_shape_ = EmotionShape::kNeutral;

    TaskHandle_t blink_task_handle_ = nullptr;         // 平静表情：随机眨眼
    TaskHandle_t eye_move_task_handle_ = nullptr;      // 平静表情：眼珠随机游走
    TaskHandle_t drift_task_handle_ = nullptr;         // 思考/连接中：缓慢左右漂移
    TaskHandle_t clock_refresh_task_handle_ = nullptr; // 锁屏：每秒刷新时间
    TaskHandle_t auto_revert_task_handle_ = nullptr;      // 惊讶表情的一次性自动回退
    TaskHandle_t control_url_task_handle_ = nullptr;

    // 协作式停止标志：不能从外部直接 vTaskDelete 这些循环任务，否则如果正好
    // 删在任务持有 DisplayLockGuard(LVGL 全局锁) 的瞬间，锁永远不会被释放，
    // 会导致整个屏幕（甚至其它用到同一把 LVGL 锁的地方）永久卡死。
    // 改为设标志位，任务在自己循环里"没持锁"的安全点检查到标志后自行退出。
    volatile bool blink_stop_ = false;
    volatile bool eye_move_stop_ = false;
    volatile bool drift_stop_ = false;
    volatile bool clock_refresh_stop_ = false;
    volatile bool auto_revert_stop_ = false;
    // Delayed tasks capture a generation number.  A newly scheduled transition
    // invalidates every older task, even when an older task wakes up after the
    // new task has reset its stop flag.
    uint32_t auto_revert_generation_ = 0;
    uint32_t control_url_generation_ = 0;

    int cur_eye_left_x_ = 0;
    int cur_eye_left_y_ = 0;
    int cur_eye_right_x_ = 0;
    int cur_eye_right_y_ = 0;

    void CreateEyes();
    void CreateLockScreen();

    void StopEyeAnimationTasks();
    void StopDriftTask();
    void StartDriftTaskIfNeeded();
    void StopClockRefreshTask();
    void StopAutoRevertTask();
    void StartZzzAnim();
    void StopZzzAnim();
    void StartSleepSway();
    void StopSleepSway();

    void ToAnyPosition(int left_x, int left_y, int right_x, int right_y);
    static int BresenhamLine(int x1, int y1, int x2, int y2, int ret[]);

    // radius 是圆角半径（LV_RADIUS_CIRCLE 表示纯圆），gap 是两眼中心到屏幕中线的距离，
    // x_offset/y_offset 是相对屏幕中心的额外偏移。gap 单独可调是为了避免眼睛变大时
    // 内侧边缘互相贴到一起（放大表情必须同步把眼距也拉开，否则会糊成一只眼睛）
    void ApplyEyeShape(int width, int height, int radius, int gap, int x_offset, int y_offset);
    void ApplyEyeShapeLocked(int width, int height, int radius, int gap, int x_offset, int y_offset);
    // 用一块底色（白色）遮罩盖住眼睛的某个角/某一侧，切出眼皮效果
    // rotation_tenths 单位是 LVGL 的 0.1 度；cx/cy 是遮罩中心相对屏幕中心的偏移
    void ApplyEyelidMask(lv_obj_t* mask, int width, int height, int radius, int rotation_tenths, int cx, int cy);
    void ApplyEyelidMaskLocked(lv_obj_t* mask, int width, int height, int radius, int rotation_tenths, int cx, int cy);
    void HideEyelids();
    void HideEyelidsLocked();
    void HideEmotionExtras();
    void HideEmotionExtrasLocked();
    EmotionShape MapEmotionToShape(const std::string& emotion);
    void ApplyEmotionShape(EmotionShape shape);

    void BlinkTask();
    void EyeMoveTask();
    void DriftTask();
    void ClockRefreshTask();
};

#endif // DOG_OLED_DISPLAY_H
