#ifndef SMART_HOME_CLIENT_H
#define SMART_HOME_CLIENT_H

#include <cJSON.h>
#include <mqtt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

// Local Pi Mosquitto client (1884): outlet fan/light, SOS, status cache, alert announce.
class SmartHomeClient {
public:
    SmartHomeClient();
    ~SmartHomeClient();

    static SmartHomeClient* GetInstance() { return instance_; }

    void Start();
    void Stop();
    bool IsConnected() const;

    // action: "on" | "off" | "toggle"
    bool SetOutlet(const std::string& device_id, const std::string& action);
    bool SendSos(const std::string& message = "");

    std::string GetStatusJson() const;
    std::string GetStatusSummaryZh() const;

    void RegisterMcpTools();

private:
    struct DeviceCache {
        bool seen = false;
        bool online = false;
        std::string name;
        std::string state;
        std::string socket_1;
        std::string gas;
        std::string gas_value;
        std::string temperature;
        std::string humidity;
        int64_t ts = 0;
    };

    struct EventCache {
        std::string type;
        std::string message;
        std::string level;
        int64_t ts = 0;
    };

    struct Snapshot {
        bool connected = false;
        std::string broker;
        DeviceCache outlet_01;
        DeviceCache outlet_02;
        DeviceCache gas_alarm_01;
        DeviceCache temp_humi_01;
        EventCache last_event;
        EventCache last_feedback;
    };

    static SmartHomeClient* instance_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    mutable std::mutex mutex_;
    mutable std::mutex mqtt_mutex_;
    std::unique_ptr<Mqtt> mqtt_;
    esp_timer_handle_t reconnect_timer_ = nullptr;
    std::atomic<bool> want_running_{false};
    std::atomic<bool> connecting_{false};
    std::atomic<bool> connect_task_running_{false};

    std::string broker_host_ = "192.168.3.88";
    int broker_port_ = 1884;
    std::string home_prefix_ = "elder/home001/";
    std::string client_id_;

    DeviceCache outlet_01_;
    DeviceCache outlet_02_;
    DeviceCache gas_alarm_01_;
    DeviceCache temp_humi_01_;
    EventCache last_event_;
    EventCache last_feedback_;
    std::string last_announce_key_;

    void LoadSettings();
    void RequestConnect();
    void Connect();
    void ScheduleReconnect();
    void SubscribeAll();
    void OnMessage(const std::string& topic, const std::string& payload);
    void HandleDeviceStatus(const std::string& device_id, cJSON* root);
    void HandleDeviceAck(const std::string& device_id, cJSON* root);
    void HandleUnifiedEvent(cJSON* root);
    void HandleFeedback(cJSON* root);
    DeviceCache* FindDevice(const std::string& device_id);
    void ApplyEntities(DeviceCache& cache, cJSON* entities);
    bool PublishJson(const std::string& topic, cJSON* root);
    std::string MakeMsgId() const;
    std::string Topic(const std::string& suffix) const;
    bool ShouldAnnounce(const std::string& key);
    void Announce(const char* status, const std::string& message, const char* emotion,
                  const std::string_view& sound);
    Snapshot TakeSnapshot() const;
    static void OnReconnectTimer(void* arg);
    static void ConnectTask(void* arg);
};

#endif  // SMART_HOME_CLIENT_H
