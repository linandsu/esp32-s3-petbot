#ifndef GREE_IR_ENCODER_H
#define GREE_IR_ENCODER_H

#include <stdint.h>

namespace iot {

class GreeIrEncoder {
public:
    enum Mode {
        MODE_AUTO = 0,
        MODE_COOL = 1,
        MODE_DRY = 2,
        MODE_FAN = 3,
        MODE_HEAT = 4
    };

    enum FanSpeed {
        FAN_AUTO = 0,
        FAN_LOW = 1,
        FAN_MEDIUM = 2,
        FAN_HIGH = 3
    };

    // 发送格力红外指令
    // gpio_num: 红外发射管连接的 GPIO
    // power: true 开机, false 关机
    // mode: 工作模式
    // temp: 目标温度 (16-30)
    // fan: 风速
    static void SendCommand(int gpio_num, bool power, Mode mode, int temp, FanSpeed fan);
};

} // namespace iot

#endif // GREE_IR_ENCODER_H
