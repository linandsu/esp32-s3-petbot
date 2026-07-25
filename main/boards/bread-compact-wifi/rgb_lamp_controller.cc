#include "rgb_lamp_controller.h"
#include "battery_monitor.h"

#include <esp_log.h>

#include <cstdio>
#include <algorithm>

#define TAG "RgbLampController"

RgbLampController* RgbLampController::instance_ = nullptr;

RgbLampController::RgbLampController(gpio_num_t strip_gpio_1, gpio_num_t strip_gpio_2, int led_num)
    : led_num_(led_num) {
    instance_ = this;
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

void RgbLampController::SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    StopEffectTask();
    red_ = r;
    green_ = g;
    blue_ = b;
    brightness_ = brightness;
    effect_ = (r == 0 && g == 0 && b == 0) ? "off" : "color";
    RenderSolid();
    RememberCurrentState();
}

void RgbLampController::UpdateColor(uint8_t r, uint8_t g, uint8_t b) {
    red_ = r;
    green_ = g;
    blue_ = b;
    if (current_effect_ == Effect::kNone) {
        if (IsOn()) {
            RenderSolid();
        } else {
            saved_red_ = r;
            saved_green_ = g;
            saved_blue_ = b;
        }
    }
    RememberCurrentState();
}

void RgbLampController::SetBrightness(uint8_t brightness) {
    brightness_ = brightness;
    if (current_effect_ == Effect::kNone) {
        if (IsOn()) {
            RenderSolid();
        } else {
            saved_brightness_ = brightness;
        }
    }
    RememberCurrentState();
}

bool RgbLampController::Configure(const std::string& effect, uint8_t r, uint8_t g, uint8_t b,
                                  uint8_t brightness) {
    if (effect == "off") {
        TurnOff();
        return true;
    }
    if (BatteryMonitor::GetInstance() && BatteryMonitor::GetInstance()->IsHighLoadBlocked()) return false;
    red_ = r;
    green_ = g;
    blue_ = b;
    brightness_ = brightness;
    const bool ok = SetEffect(effect);
    if (ok) RememberCurrentState();
    return ok;
}

void RgbLampController::TurnOff() {
    if (IsOn()) RememberCurrentState();
    StopEffectTask();
    effect_ = "off";
    SetAllPixels(0, 0, 0);
}

bool RgbLampController::TogglePower() {
    if (IsOn()) {
        TurnOff();
        return false;
    }
    Configure(saved_effect_, saved_red_, saved_green_, saved_blue_, saved_brightness_);
    return true;
}

bool RgbLampController::IsOn() const {
    return effect_ != "off" && brightness_ > 0;
}

void RgbLampController::RememberCurrentState() {
    if (!IsOn()) return;
    saved_red_ = red_;
    saved_green_ = green_;
    saved_blue_ = blue_;
    saved_brightness_ = brightness_;
    saved_effect_ = effect_;
}

bool RgbLampController::SetEffect(const std::string& effect) {
    if (effect == "off") {
        TurnOff();
    } else if (BatteryMonitor::GetInstance() && BatteryMonitor::GetInstance()->IsHighLoadBlocked()) {
        return false;
    } else if (effect == "flash") {
        effect_ = "flash";
        StartEffect(Effect::kFlashlight);
    } else if (effect == "breathe") {
        effect_ = "breathe";
        StartEffect(Effect::kBreathe);
    } else if (effect == "flow") {
        effect_ = "flow";
        StartEffect(Effect::kFlow);
    } else if (effect == "neon") {
        effect_ = "neon";
        StartEffect(Effect::kNeon);
    } else if (effect == "rainbow") {
        effect_ = "rainbow";
        StartEffect(Effect::kRainbow);
    } else if (effect == "color") {
        SetColor(red_, green_, blue_, brightness_);
    } else {
        return false;
    }
    RememberCurrentState();
    if (effect != "off" && BatteryMonitor::GetInstance()) BatteryMonitor::GetInstance()->HoldLevelUpdates(3000);
    return true;
}

std::string RgbLampController::GetStatusJson() const {
    char json[96];
    snprintf(json, sizeof(json), "{\"r\":%u,\"g\":%u,\"b\":%u,\"brightness\":%u,\"effect\":\"%s\"}",
             red_, green_, blue_, brightness_, effect_.c_str());
    return json;
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

void RgbLampController::RenderSolid() {
    SetAllPixels(static_cast<uint8_t>((static_cast<uint16_t>(red_) * brightness_) / 255),
                 static_cast<uint8_t>((static_cast<uint16_t>(green_) * brightness_) / 255),
                 static_cast<uint8_t>((static_cast<uint16_t>(blue_) * brightness_) / 255));
}

void RgbLampController::StopEffectTask() {
    ++effect_generation_;
    current_effect_ = Effect::kNone;
    effect_task_handle_ = nullptr;
}

void RgbLampController::RunFlashlight(uint32_t generation) {
    while (generation == effect_generation_) {
        RenderSolid();
        vTaskDelay(pdMS_TO_TICKS(180));
        SetAllPixels(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(180));
    }
}

void RgbLampController::RunBreathe(uint32_t generation) {
    int level = 0;
    int step = 5;
    while (generation == effect_generation_) {
        const uint16_t scale = static_cast<uint16_t>(brightness_) * level / 255;
        SetAllPixels(red_ * scale / 255, green_ * scale / 255, blue_ * scale / 255);
        level += step;
        if (level >= 255) { level = 255; step = -5; }
        if (level <= 4) { level = 4; step = 5; }
        vTaskDelay(pdMS_TO_TICKS(24));
    }
}

void RgbLampController::RunFlow(uint32_t generation) {
    int head = 0;
    while (generation == effect_generation_) {
        for (int i = 0; i < led_num_; ++i) {
            const int distance = (head - i + led_num_) % led_num_;
            const uint8_t intensity = distance == 0 ? 255 : (distance == 1 ? 85 : 18);
            const uint16_t scale = static_cast<uint16_t>(brightness_) * intensity / 255;
            const uint8_t r = red_ * scale / 255;
            const uint8_t g = green_ * scale / 255;
            const uint8_t b = blue_ * scale / 255;
            led_strip_set_pixel(strip_1_, i, r, g, b);
            led_strip_set_pixel(strip_2_, led_num_ - 1 - i, r, g, b);
        }
        led_strip_refresh(strip_1_);
        led_strip_refresh(strip_2_);
        head = (head + 1) % led_num_;
        vTaskDelay(pdMS_TO_TICKS(130));
    }
}

void RgbLampController::RunNeon(uint32_t generation) {
    int phase = 0;
    while (generation == effect_generation_) {
        for (int i = 0; i < led_num_; ++i) {
            int wave = (phase + i * 73) & 0xff;
            wave = wave < 128 ? wave * 2 : (255 - wave) * 2;
            const uint8_t intensity = 75 + wave * 180 / 255;
            const uint16_t scale = static_cast<uint16_t>(brightness_) * intensity / 255;
            led_strip_set_pixel(strip_1_, i, red_ * scale / 255, green_ * scale / 255, blue_ * scale / 255);
            led_strip_set_pixel(strip_2_, i, red_ * scale / 255, green_ * scale / 255, blue_ * scale / 255);
        }
        led_strip_refresh(strip_1_);
        led_strip_refresh(strip_2_);
        phase = (phase + 9) & 0xff;
        vTaskDelay(pdMS_TO_TICKS(45));
    }
}

static void ColorWheel(uint8_t position, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (position < 85) {
        r = 255 - position * 3; g = position * 3; b = 0;
    } else if (position < 170) {
        position -= 85; r = 0; g = 255 - position * 3; b = position * 3;
    } else {
        position -= 170; r = position * 3; g = 0; b = 255 - position * 3;
    }
}

void RgbLampController::RunRainbow(uint32_t generation) {
    uint8_t phase = 0;
    while (generation == effect_generation_) {
        for (int i = 0; i < led_num_; ++i) {
            uint8_t r, g, b;
            ColorWheel(static_cast<uint8_t>(phase + i * 256 / led_num_), r, g, b);
            r = static_cast<uint16_t>(r) * brightness_ / 255;
            g = static_cast<uint16_t>(g) * brightness_ / 255;
            b = static_cast<uint16_t>(b) * brightness_ / 255;
            led_strip_set_pixel(strip_1_, i, r, g, b);
            led_strip_set_pixel(strip_2_, led_num_ - 1 - i, r, g, b);
        }
        led_strip_refresh(strip_1_);
        led_strip_refresh(strip_2_);
        phase += 5;
        vTaskDelay(pdMS_TO_TICKS(45));
    }
}

void RgbLampController::StartEffect(Effect effect) {
    StopEffectTask();
    current_effect_ = effect;
    const uint32_t generation = effect_generation_;
    struct EffectContext { RgbLampController* controller; Effect effect; uint32_t generation; };
    auto* context = new EffectContext{this, effect, generation};
    const BaseType_t result = xTaskCreate([](void* arg) {
        auto* context = static_cast<EffectContext*>(arg);
        auto* controller = context->controller;
        const auto effect = context->effect;
        const auto generation = context->generation;
        delete context;
        switch (effect) {
            case Effect::kFlashlight: controller->RunFlashlight(generation); break;
            case Effect::kBreathe: controller->RunBreathe(generation); break;
            case Effect::kFlow: controller->RunFlow(generation); break;
            case Effect::kNeon: controller->RunNeon(generation); break;
            case Effect::kRainbow: controller->RunRainbow(generation); break;
            default: break;
        }
        if (generation == controller->effect_generation_) controller->effect_task_handle_ = nullptr;
        vTaskDelete(nullptr);
    }, "lamp_effect", 3072, context, 3, &effect_task_handle_);
    if (result != pdPASS) {
        delete context;
        current_effect_ = Effect::kNone;
        effect_task_handle_ = nullptr;
        ESP_LOGE(TAG, "Failed to create lamp effect task");
    }
}

void RgbLampController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.lamp.turn_on",
        "打开小狗身上的 RGB 灯带，默认显示白色常亮灯。"
        "用户说「开灯」「关灯」且未提「房间」时用本工具；不要调用智能家居房间灯工具。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            SetColor(255, 255, 255);
            return true;
        });

    mcp_server.AddTool(
        "self.lamp.turn_off",
        "关闭小狗身上的 RGB 灯带和当前灯效。"
        "用户说「关灯」且未提「房间」时用本工具。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            TurnOff();
            return true;
        });

    mcp_server.AddTool(
        "self.lamp.set",
        "设置小狗身上 RGB 灯带的颜色、亮度和灯效。用户说“打开黄色呼吸灯”“蓝色流水灯”“粉色霓虹灯”或笼统「开灯」时调用此工具。"
        "不要用于控制家里的房间灯；房间灯请用 self.smarthome.set_room_light。"
        "effect 可选：color（常亮）、breathe（呼吸）、flash（闪烁）、flow（流水）、neon（霓虹）、rainbow（彩虹）、off（关闭）。"
        "把用户说的颜色换算为 RGB；rainbow 会忽略 RGB。brightness 为 0 到 255。",
        PropertyList({
            Property("effect", kPropertyTypeString, "color"),
            Property("red", kPropertyTypeInteger, 255, 0, 255),
            Property("green", kPropertyTypeInteger, 255, 0, 255),
            Property("blue", kPropertyTypeInteger, 255, 0, 255),
            Property("brightness", kPropertyTypeInteger, 255, 0, 255),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto effect = properties["effect"].value<std::string>();
            const int r = properties["red"].value<int>();
            const int g = properties["green"].value<int>();
            const int b = properties["blue"].value<int>();
            const int brightness = properties["brightness"].value<int>();
            if (!Configure(effect, r, g, b, brightness)) {
                return "不支持的灯效。可用灯效：color, breathe, flash, flow, neon, rainbow, off";
            }
            return true;
        });

    ESP_LOGI(TAG, "RGB lamp MCP tools registered");
}
