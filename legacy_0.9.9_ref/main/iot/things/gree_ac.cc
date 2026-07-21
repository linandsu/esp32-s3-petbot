#include "iot/thing.h"
#include "board.h"
#include <esp_log.h>
#include <hal/gpio_types.h>
#include "gree_ir_encoder.h"

#define TAG "GreeAC"

#ifndef IR_TX_GPIO_NUM
#define IR_TX_GPIO_NUM GPIO_NUM_9 // 默认引脚
#endif

namespace iot {

class GreeAC : public Thing {
private:
    bool power_ = false;
    GreeIrEncoder::Mode mode_ = GreeIrEncoder::MODE_COOL;
    int temp_ = 26;
    GreeIrEncoder::FanSpeed fan_ = GreeIrEncoder::FAN_AUTO;

    void SyncState() {
        ESP_LOGI(TAG, "Syncing state to Gree AC: Power=%d, Mode=%d, Temp=%d, Fan=%d", power_, mode_, temp_, fan_);
        GreeIrEncoder::SendCommand(IR_TX_GPIO_NUM, power_, mode_, temp_, fan_);
    }

public:
    GreeAC() : Thing("GreeAC", "格力空调") {
        
        // 注册属性 (供大模型读取当前状态)
        properties_.AddBooleanProperty("power", "是否开机", [this]() { return power_; });
        properties_.AddNumberProperty("temperature", "当前设定温度", [this]() { return temp_; });
        
        // 注册方法 (供大模型调用)
        methods_.AddMethod("TurnOn", "打开空调", ParameterList(), [this](const ParameterList& parameters) {
            power_ = true;
            SyncState();
        });

        methods_.AddMethod("TurnOff", "关闭空调", ParameterList(), [this](const ParameterList& parameters) {
            power_ = false;
            SyncState();
        });

        ParameterList temp_params;
        temp_params.AddParameter(Parameter("temperature", "目标温度 (16-30)", kValueTypeNumber));
        methods_.AddMethod("SetTemperature", "设置空调温度", temp_params, [this](const ParameterList& parameters) {
            int t = parameters["temperature"].number();
            if (t >= 16 && t <= 30) {
                temp_ = t;
                SyncState();
            } else {
                ESP_LOGW(TAG, "Invalid temperature: %d", t);
            }
        });

        ParameterList mode_params;
        mode_params.AddParameter(Parameter("mode", "工作模式 (0:自动, 1:制冷, 2:抽湿, 3:送风, 4:制热)", kValueTypeNumber));
        methods_.AddMethod("SetMode", "设置空调工作模式", mode_params, [this](const ParameterList& parameters) {
            int m = parameters["mode"].number();
            if (m >= 0 && m <= 4) {
                mode_ = (GreeIrEncoder::Mode)m;
                SyncState();
            } else {
                ESP_LOGW(TAG, "Invalid mode: %d", m);
            }
        });
    }
};

} // namespace iot

DECLARE_THING(GreeAC);
