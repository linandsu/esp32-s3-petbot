#include "sntp_clock.h"

#include <ctime>
#include <cstdlib>
#include <cstdio>

#include <esp_netif_sntp.h>
#include <esp_log.h>

#define TAG "SntpClock"

namespace {
bool g_started = false;
bool g_synced = false;

void OnTimeSync(struct timeval*) {
    g_synced = true;
    ESP_LOGI(TAG, "SNTP 时间同步完成");
}
} // namespace

void SntpClock::EnsureStarted() {
    if (g_started) {
        return;
    }
    g_started = true;

    // 固定东八区（北京时间），无夏令时
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    config.wait_for_sync = false;
    config.sync_cb = OnTimeSync;
    esp_netif_sntp_init(&config);
}

bool SntpClock::IsTimeValid() {
    if (g_synced) {
        return true;
    }
    // 兜底：即使没收到同步回调，只要系统时间已经明显超过 2020 年，也认为有效
    // （例如某些场景下回调注册时机晚于同步完成）
    time_t now = time(nullptr);
    return now > 1577836800; // 2020-01-01
}

std::string SntpClock::GetTimeString() {
    time_t now = time(nullptr);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
    return std::string(buf);
}

std::string SntpClock::GetDateString() {
    static const char* kWeekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    time_t now = time(nullptr);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d月%d日 %s", local_tm.tm_mon + 1, local_tm.tm_mday, kWeekdays[local_tm.tm_wday]);
    return std::string(buf);
}
