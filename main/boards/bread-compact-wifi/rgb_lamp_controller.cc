#include "rgb_lamp_controller.h"

#include <esp_log.h>

#define TAG "RgbLampController"

RgbLampController::RgbLampController(gpio_num_t strip_gpio_1, gpio_num_t strip_gpio_2, int led_num)
    : led_num_(led_num) {
    led_strip_config_t strip_config_1 = {};
    strip_config_1.strip_gpio_num = strip_gpio_1;
    strip_config_1.max_leds = led_num_;
    strip_config_1.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_config_t strip_config_2 = {};
    strip_config_2.strip_gpio_num = strip_gpio_2;
    strip_config_2.max_leds = led_num_;
    strip_config_2.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config_1, &rmt_config, &strip_1_));
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config_2, &rmt_config, &strip_2_));

    led_strip_clear(strip_1_);
    led_strip_clear(strip_2_);

    RegisterMcpTools();
}

RgbLampController::~RgbLampController() {
    StopEffectTask();
    if (strip_1_ != nullptr) {
        led_strip_del(strip_1_);
    }
    if (strip_2_ != nullptr) {
        led_strip_del(strip_2_);
    }
}

void RgbLampController::SetAllPixels(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < led_num_; i++) {
        led_strip_set_pixel(strip_1_, i, r, g, b);
        led_strip_set_pixel(strip_2_, i, r, g, b);
    }
    led_strip_refresh(strip_1_);
    led_strip_refresh(strip_2_);
}

void RgbLampController::StopEffectTask() {
    if (effect_task_handle_ != nullptr) {
        vTaskDelete(effect_task_handle_);
        effect_task_handle_ = nullptr;
    }
}

void RgbLampController::RunFlashlight() {
    int count = 5;
    while (count--) {
        SetAllPixels(255, 255, 255);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        SetAllPixels(0, 0, 0);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    effect_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void RgbLampController::RunBreathe() {
    int count = 5;
    while (count--) {
        for (int i = 0; i < 256; i++) {
            SetAllPixels(i, i, i);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        for (int i = 255; i > 0; i--) {
            SetAllPixels(i, i, i);
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
    effect_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void RgbLampController::StartEffect(Effect effect) {
    StopEffectTask();

    switch (effect) {
        case Effect::kFlashlight:
            xTaskCreate([](void* arg) {
                auto this_ = (RgbLampController*)arg;
                this_->RunFlashlight();
            }, "lamp_flashlight", 2048, this, 3, &effect_task_handle_);
            break;
        case Effect::kBreathe:
            xTaskCreate([](void* arg) {
                auto this_ = (RgbLampController*)arg;
                this_->RunBreathe();
            }, "lamp_breathe", 2048, this, 3, &effect_task_handle_);
            break;
        default:
            break;
    }
}

void RgbLampController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool("self.lamp.turn_on", "打开小狗身上的 RGB 灯带（常亮白光）", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            StopEffectTask();
            SetAllPixels(255, 255, 255);
            return true;
        });

    mcp_server.AddTool("self.lamp.turn_off", "关闭小狗身上的 RGB 灯带", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            StopEffectTask();
            SetAllPixels(0, 0, 0);
            return true;
        });

    mcp_server.AddTool("self.lamp.flashlight", "让灯带像手电筒一样连续闪烁几次", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            StartEffect(Effect::kFlashlight);
            return true;
        });

    mcp_server.AddTool("self.lamp.breathe", "让灯带做呼吸灯效果（渐亮渐暗循环）", PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            StartEffect(Effect::kBreathe);
            return true;
        });

    ESP_LOGI(TAG, "RGB 灯带 MCP 工具注册完成");
}
