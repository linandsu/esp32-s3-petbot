#ifndef RGB_LAMP_CONTROLLER_H
#define RGB_LAMP_CONTROLLER_H

#include "mcp_server.h"
#include "led_strip.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string>
#include <atomic>

// 双路 WS2812 灯带控制器，从 0.9.9 版本的 iot/things/lamp.cc 迁移为 MCP 工具。
// 保持原有引脚：GPIO38 / GPIO8，每路 4 颗灯珠。
class RgbLampController {
public:
    RgbLampController(gpio_num_t strip_gpio_1, gpio_num_t strip_gpio_2, int led_num = 4);
    ~RgbLampController();

    static RgbLampController* GetInstance() { return instance_; }
    void SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 255);
    void UpdateColor(uint8_t r, uint8_t g, uint8_t b);
    void SetBrightness(uint8_t brightness);
    bool Configure(const std::string& effect, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
    void TurnOff();
    bool TogglePower();
    bool IsOn() const;
    bool SetEffect(const std::string& effect);
    std::string GetStatusJson() const;

private:
    static constexpr int kLedNum = 4;

    led_strip_handle_t strip_1_ = nullptr;
    led_strip_handle_t strip_2_ = nullptr;
    static RgbLampController* instance_;
    int led_num_;
    uint8_t red_ = 0, green_ = 0, blue_ = 0, brightness_ = 255;
    std::string effect_ = "off";
    uint8_t saved_red_ = 255, saved_green_ = 255, saved_blue_ = 255, saved_brightness_ = 255;
    std::string saved_effect_ = "color";

    TaskHandle_t effect_task_handle_ = nullptr;

    enum class Effect {
        kNone,
        kFlashlight,
        kBreathe,
        kFlow,
        kNeon,
        kRainbow,
    };

    std::atomic<uint32_t> effect_generation_{0};
    std::atomic<Effect> current_effect_{Effect::kNone};

    void SetAllPixels(uint8_t r, uint8_t g, uint8_t b);
    void StopEffectTask();
    void StartEffect(Effect effect);
    void RunFlashlight(uint32_t generation);
    void RunBreathe(uint32_t generation);
    void RunFlow(uint32_t generation);
    void RunNeon(uint32_t generation);
    void RunRainbow(uint32_t generation);
    void RenderSolid();
    void RememberCurrentState();

    void RegisterMcpTools();
};

#endif // RGB_LAMP_CONTROLLER_H
