#ifndef SNTP_CLOCK_H
#define SNTP_CLOCK_H

#include <string>

// 极简的 SNTP 校时封装，供锁屏界面显示时间/日期使用。
// 固定使用东八区（北京时间，UTC+8，无夏令时）。
class SntpClock {
public:
    // 幂等，可以多次调用（内部只会真正初始化一次）
    static void EnsureStarted();

    // 是否已经完成过一次时间同步（同步之前系统时间是从 1970 年开始走的，不适合展示）
    static bool IsTimeValid();

    // 形如 "14:32"
    static std::string GetTimeString();
    // 形如 "7月21日 星期二"
    static std::string GetDateString();
};

#endif // SNTP_CLOCK_H
