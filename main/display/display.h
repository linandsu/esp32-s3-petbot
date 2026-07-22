#ifndef DISPLAY_H
#define DISPLAY_H

#include "emoji_collection.h"
#include "text_glyph.h"

#ifndef CONFIG_USE_EMOTE_MESSAGE_STYLE
#define HAVE_LVGL 1
#include <lvgl.h>
#endif

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class Theme {
public:
    Theme(const std::string& name) : name_(name) {}
    virtual ~Theme() = default;

    inline std::string name() const { return name_; }

private:
    std::string name_;
};

class Display {
public:
    Display();
    virtual ~Display();

    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000);
    virtual void SetEmotion(const char* emotion);
    virtual void SetChatMessage(const char* role, const char* content);
    virtual void ClearChatMessages();
    virtual void SetTheme(Theme* theme);
    virtual Theme* GetTheme() { return current_theme_; }
    virtual void UpdateStatusBar(bool update_all = false);
    virtual void SetPowerSaveMode(bool on);
    virtual bool AddTextGlyphs(const std::vector<TextGlyph>& glyphs, uint8_t bpp) { return false; }
    virtual void ClearTextGlyphs() {}
    virtual void SetEmojiCollection(std::shared_ptr<EmojiCollection>) {}
    virtual void SetupUI() { setup_ui_called_ = true; }

    // Dog pet screen hooks (used by boards with a custom "face" UI, e.g. the pet dog OLED).
    // Exactly one of these three is active at a time, driven externally by DogController
    // based on device conversation state + idle duration.
    virtual void ShowDogFace() {}          // 正常对话时的默认页面：睁眼的小狗表情
    virtual void ShowDogStartup() {}       // 开机联网/激活期间显示标准初始化 UI
    virtual void ShowDogSleepFace() {}     // 刚进入待机时：呼呼大睡的小狗表情
    virtual void ShowLockScreen() {}       // 待机超过一定时长后：时间/日期锁屏页面
    virtual void ShowControlUrl(const char* url, int duration_ms = 15000) {}

    inline int width() const { return width_; }
    inline int height() const { return height_; }
    inline bool IsSetupUICalled() const { return setup_ui_called_; }

protected:
    int width_ = 0;
    int height_ = 0;
    bool setup_ui_called_ = false;  // Track if SetupUI() has been called

    Theme* current_theme_ = nullptr;

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;
};

class DisplayLockGuard {
public:
    DisplayLockGuard(Display* display) : display_(display) {
        if (!display_->Lock(30000)) {
            ESP_LOGE("Display", "Failed to lock display");
        }
    }
    ~DisplayLockGuard() { display_->Unlock(); }

private:
    Display* display_;
};

class NoDisplay : public Display {
private:
    virtual bool Lock(int timeout_ms = 0) override { return true; }
    virtual void Unlock() override {}
};

#endif
