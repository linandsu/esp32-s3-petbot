#ifndef RGB_LAMP_CONTROLLER_H
#define RGB_LAMP_CONTROLLER_H

#include "mcp_server.h"
#include "led_strip.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 双路 WS2812 灯带控制器，从 0.9.9 版本的 iot/things/lamp.cc 迁移为 MCP 工具。
// 保持原有引脚：GPIO38 / GPIO8，每路 4 颗灯珠。
class RgbLampController {
public:
    RgbLampController(gpio_num_t strip_gpio_1, gpio_num_t strip_gpio_2, int led_num = 4);
    ~RgbLampController();

private:
    static constexpr int kLedNum = 4;

    led_strip_handle_t strip_1_ = nullptr;
    led_strip_handle_t strip_2_ = nullptr;
    int led_num_;

    TaskHandle_t effect_task_handle_ = nullptr;

    enum class Effect {
        kNone,
        kFlashlight,
        kBreathe,
    };

    void SetAllPixels(uint8_t r, uint8_t g, uint8_t b);
    void StopEffectTask();
    void StartEffect(Effect effect);
    void RunFlashlight();
    void RunBreathe();

    void RegisterMcpTools();
};

#endif // RGB_LAMP_CONTROLLER_H
