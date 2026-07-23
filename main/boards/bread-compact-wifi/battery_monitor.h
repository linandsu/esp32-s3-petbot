#pragma once

#include <atomic>
#include <esp_timer.h>
#include <string>

class BatteryMonitor {
public:
    enum class Level { kUnknown, kHigh, kNormal, kMedium, kLow, kCritical, kProtect };
    BatteryMonitor();
    ~BatteryMonitor();
    static BatteryMonitor* GetInstance() { return instance_; }
    void HoldLevelUpdates(uint32_t milliseconds);
    bool IsHighLoadBlocked() const { return level_.load() >= static_cast<int>(Level::kCritical); }
    bool IsProtecting() const { return level_.load() == static_cast<int>(Level::kProtect); }
    std::string GetStatusJson() const;
    std::string GetVoiceSummary() const;
private:
    static BatteryMonitor* instance_;
    void Sample();
    static void TimerCallback(void* arg);
    static Level LevelFor(float voltage);
    static int PercentFor(float voltage);
    std::atomic<int> level_{static_cast<int>(Level::kUnknown)};
    std::atomic<int> voltage_mv_{0};
    std::atomic<int> percent_{0};
    std::atomic<int64_t> hold_until_us_{0};
    int64_t last_low_notice_us_ = 0;
    int64_t last_critical_notice_us_ = 0;
    int candidate_ = static_cast<int>(Level::kUnknown);
    int candidate_count_ = 0;
    esp_timer_handle_t timer_ = nullptr;
    void* adc_handle_ = nullptr;
    void* cali_handle_ = nullptr;
};
