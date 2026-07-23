#include "battery_monitor.h"
#include "config.h"
#include "application.h"
#include "dog_controller.h"
#include "rgb_lamp_controller.h"
#include "mcp_server.h"
#include "board.h"
#include "display.h"
#include "dog_oled_display.h"

#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <algorithm>
#include <cstdio>

static constexpr char TAG[] = "BatteryMonitor";
BatteryMonitor* BatteryMonitor::instance_ = nullptr;

BatteryMonitor::BatteryMonitor() {
    instance_ = this;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    adc_oneshot_unit_handle_t handle = nullptr;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &handle));
    adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, BATTERY_ADC_CHANNEL, &channel));
    adc_handle_ = handle;
    adc_cali_curve_fitting_config_t cali = {.unit_id = ADC_UNIT_1, .chan = BATTERY_ADC_CHANNEL, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_cali_handle_t cali_handle = nullptr;
    if (adc_cali_create_scheme_curve_fitting(&cali, &cali_handle) == ESP_OK) cali_handle_ = cali_handle;
    esp_timer_create_args_t args = {.callback = &BatteryMonitor::TimerCallback, .arg = this, .dispatch_method = ESP_TIMER_TASK, .name = "dog_battery"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_once(timer_, 8000000));
    McpServer::GetInstance().AddTool("self.battery.get_status",
        "查询小狗当前电池电压、电量百分比和低电保护状态。用户询问电量、电压、还剩多少电、是否低电时必须调用此工具。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto* battery = BatteryMonitor::GetInstance();
            return battery ? battery->GetVoiceSummary() : "电量检测尚未初始化。";
        });
}

BatteryMonitor::~BatteryMonitor() {
    if (timer_) esp_timer_delete(timer_);
    if (cali_handle_) adc_cali_delete_scheme_curve_fitting(static_cast<adc_cali_handle_t>(cali_handle_));
    if (adc_handle_) adc_oneshot_del_unit(static_cast<adc_oneshot_unit_handle_t>(adc_handle_));
}

void BatteryMonitor::TimerCallback(void* arg) {
    auto* self = static_cast<BatteryMonitor*>(arg);
    self->Sample();
    esp_timer_start_once(self->timer_, 10000000);
}

void BatteryMonitor::HoldLevelUpdates(uint32_t milliseconds) {
    hold_until_us_.store(esp_timer_get_time() + static_cast<int64_t>(milliseconds) * 1000);
}

BatteryMonitor::Level BatteryMonitor::LevelFor(float v) {
    if (v < 3.40f) return Level::kProtect;
    if (v < 3.55f) return Level::kCritical;
    if (v < 3.70f) return Level::kLow;
    if (v < 3.85f) return Level::kMedium;
    if (v < 4.05f) return Level::kNormal;
    return Level::kHigh;
}

int BatteryMonitor::PercentFor(float v) {
    struct Point { float v; int p; }; static constexpr Point points[] = {{4.20f,100},{4.05f,85},{3.90f,65},{3.80f,45},{3.70f,25},{3.55f,12},{3.40f,3},{3.20f,0}};
    if (v >= points[0].v) return 100;
    for (size_t i = 0; i + 1 < sizeof(points)/sizeof(points[0]); ++i) if (v >= points[i+1].v) return points[i+1].p + static_cast<int>((v-points[i+1].v)*(points[i].p-points[i+1].p)/(points[i].v-points[i+1].v));
    return 0;
}

void BatteryMonitor::Sample() {
    auto adc = static_cast<adc_oneshot_unit_handle_t>(adc_handle_);
    int readings[16];
    for (int& raw : readings) { if (adc_oneshot_read(adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) return; }
    std::sort(std::begin(readings), std::end(readings));
    int raw = 0; for (int i = 2; i < 14; ++i) raw += readings[i]; raw /= 12;
    int mv = raw;
    if (cali_handle_) adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(cali_handle_), raw, &mv);
    const float battery_v = mv * 2.0f / 1000.0f;
    voltage_mv_.store(static_cast<int>(battery_v * 1000)); percent_.store(PercentFor(battery_v));
    const int64_t now_us = esp_timer_get_time();
    if (now_us < hold_until_us_.load()) return;
    const int next = static_cast<int>(LevelFor(battery_v));
    if (next != candidate_) { candidate_ = next; candidate_count_ = 1; return; }
    if (++candidate_count_ < 3) return;
    if (next == level_.load()) {
        if (auto* display = dynamic_cast<DogOledDisplay*>(Board::GetInstance().GetDisplay())) {
            display->UpdateBatteryIndicator(next, battery_v, percent_.load());
            if (next == static_cast<int>(Level::kLow) && now_us - last_low_notice_us_ >= 600000000LL) {
                display->ShowBatteryNotice(false, battery_v, percent_.load());
                last_low_notice_us_ = now_us;
            } else if (next == static_cast<int>(Level::kCritical) &&
                       now_us - last_critical_notice_us_ >= 300000000LL) {
                display->ShowBatteryNotice(true, battery_v, percent_.load());
                last_critical_notice_us_ = now_us;
            }
        }
        return;
    }
    level_.store(next);
    ESP_LOGW(TAG, "Battery %.3fV level %d", battery_v, next);
    if (next >= static_cast<int>(Level::kCritical)) {
        if (auto* lamp = RgbLampController::GetInstance()) lamp->TurnOff();
    }
    if (next == static_cast<int>(Level::kProtect)) Application::GetInstance().EnterSleepMode();
    if (auto* display = dynamic_cast<DogOledDisplay*>(Board::GetInstance().GetDisplay())) {
        display->UpdateBatteryIndicator(next, battery_v, percent_.load());
        if (next == static_cast<int>(Level::kLow)) {
            display->ShowBatteryNotice(false, battery_v, percent_.load());
            last_low_notice_us_ = now_us;
        } else if (next == static_cast<int>(Level::kCritical)) {
            display->ShowBatteryNotice(true, battery_v, percent_.load());
            last_critical_notice_us_ = now_us;
        }
    }
    if (auto* display = Board::GetInstance().GetDisplay()) {
        if (next == static_cast<int>(Level::kLow)) display->ShowNotification("电量低，请充电", 2000);
        else if (next == static_cast<int>(Level::kCritical)) display->ShowNotification("严重低电量\n已关闭灯光和动作", 5000);
    }
}

std::string BatteryMonitor::GetStatusJson() const {
    static constexpr const char* names[] = {"unknown","high","normal","medium","low","critical","protect"};
    const int level = level_.load(); char json[128];
    snprintf(json, sizeof(json), "{\"voltage\":%.3f,\"percent\":%d,\"level\":\"%s\",\"high_load_blocked\":%s,\"protecting\":%s}", voltage_mv_.load()/1000.0, percent_.load(), names[level], IsHighLoadBlocked()?"true":"false", IsProtecting()?"true":"false");
    return json;
}

std::string BatteryMonitor::GetVoiceSummary() const {
    static constexpr const char* names[] = {"正在初始化", "高电量", "电量正常", "中等电量", "低电量", "严重低电量", "低电保护中"};
    const int level = level_.load();
    char text[128];
    snprintf(text, sizeof(text), "当前电池电压%.2f伏，估算电量%d%%，状态为%s。", voltage_mv_.load() / 1000.0, percent_.load(), names[level]);
    if (level >= static_cast<int>(Level::kCritical)) {
        return std::string(text) + "彩灯和动作已限制，请尽快充电。";
    }
    return text;
}
