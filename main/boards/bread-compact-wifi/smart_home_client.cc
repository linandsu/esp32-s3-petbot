#include "smart_home_client.h"

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "mcp_server.h"
#include "settings.h"
#include "system_info.h"

#include <esp_log.h>
#include <esp_random.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TAG "SmartHome"
#define SMART_HOME_RECONNECT_MS 10000

SmartHomeClient* SmartHomeClient::instance_ = nullptr;

namespace {

const char* JsonString(cJSON* obj, const char* key, const char* fallback = "") {
    auto* item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

int64_t JsonInt64(cJSON* obj, const char* key) {
    auto* item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item)) return static_cast<int64_t>(item->valuedouble);
    return 0;
}

bool JsonBool(cJSON* obj, const char* key, bool fallback = false) {
    auto* item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return fallback;
}

std::string EntityString(cJSON* entities, const char* key) {
    if (!cJSON_IsObject(entities)) return "";
    auto* item = cJSON_GetObjectItem(entities, key);
    if (cJSON_IsString(item)) return item->valuestring;
    if (cJSON_IsNumber(item)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", item->valuedouble);
        return buf;
    }
    return "";
}

std::string ActionToMqtt(const std::string& action) {
    if (action == "on" || action == "turn_on") return "turn_on";
    if (action == "off" || action == "turn_off") return "turn_off";
    if (action == "toggle") return "toggle";
    return "";
}

std::string OnOffZh(const std::string& value) {
    if (value == "on") return "开";
    if (value == "off") return "关";
    return value.empty() ? "未知" : value;
}

// Device reports Fahrenheit; convert to Celsius for UI / voice summary.
std::string FahrenheitToCelsiusString(const std::string& fahrenheit) {
    char* end = nullptr;
    const double f = strtod(fahrenheit.c_str(), &end);
    if (end == fahrenheit.c_str()) return fahrenheit;
    const double c = (f - 32.0) * 5.0 / 9.0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", c);
    return buf;
}

}  // namespace

SmartHomeClient::SmartHomeClient() {
    instance_ = this;
    LoadSettings();
    esp_timer_create_args_t args = {
        .callback = &SmartHomeClient::OnReconnectTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "smarthome_reconn",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &reconnect_timer_));
    RegisterMcpTools();
}

SmartHomeClient::~SmartHomeClient() {
    *alive_ = false;
    Stop();
    if (reconnect_timer_ != nullptr) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
        reconnect_timer_ = nullptr;
    }
    if (instance_ == this) instance_ = nullptr;
}

void SmartHomeClient::LoadSettings() {
    Settings settings("smarthome", false);
    const auto host = settings.GetString("host", broker_host_);
    if (!host.empty()) broker_host_ = host;
    broker_port_ = settings.GetInt("port", broker_port_);
    const auto prefix = settings.GetString("prefix", home_prefix_);
    if (!prefix.empty()) home_prefix_ = prefix;
    if (!home_prefix_.empty() && home_prefix_.back() != '/') home_prefix_.push_back('/');

    auto mac = SystemInfo::GetMacAddress();
    std::string suffix = mac.size() >= 5 ? mac.substr(mac.size() - 5) : mac;
    for (char& c : suffix) {
        if (c == ':') c = '_';
    }
    client_id_ = settings.GetString("client_id", "dog_robot_" + suffix);
}

void SmartHomeClient::Start() {
    want_running_ = true;
    Connect();
}

void SmartHomeClient::Stop() {
    want_running_ = false;
    if (reconnect_timer_ != nullptr) esp_timer_stop(reconnect_timer_);
    connecting_ = false;
    mqtt_.reset();
    ESP_LOGI(TAG, "Smart home MQTT stopped");
}

bool SmartHomeClient::IsConnected() const {
    return mqtt_ && mqtt_->IsConnected();
}

void SmartHomeClient::ScheduleReconnect() {
    if (!want_running_ || reconnect_timer_ == nullptr) return;
    esp_timer_stop(reconnect_timer_);
    esp_timer_start_once(reconnect_timer_, SMART_HOME_RECONNECT_MS * 1000ULL);
}

void SmartHomeClient::OnReconnectTimer(void* arg) {
    auto* self = static_cast<SmartHomeClient*>(arg);
    auto alive = self->alive_;
    Application::GetInstance().Schedule([self, alive]() {
        if (!*alive || !self->want_running_) return;
        ESP_LOGI(TAG, "Reconnecting smart home MQTT");
        self->Connect();
    });
}

void SmartHomeClient::Connect() {
    if (!want_running_ || connecting_) return;
    connecting_ = true;

    mqtt_.reset();

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "No network interface");
        connecting_ = false;
        ScheduleReconnect();
        return;
    }

    mqtt_ = network->CreateMqtt(1);
    mqtt_->SetKeepAlive(60);

    mqtt_->OnDisconnected([this]() {
        ESP_LOGW(TAG, "Disconnected from %s:%d", broker_host_.c_str(), broker_port_);
        connecting_ = false;
        ScheduleReconnect();
    });

    mqtt_->OnConnected([this]() {
        ESP_LOGI(TAG, "Connected to %s:%d as %s", broker_host_.c_str(), broker_port_,
                 client_id_.c_str());
        connecting_ = false;
        if (reconnect_timer_ != nullptr) esp_timer_stop(reconnect_timer_);
        SubscribeAll();
    });

    mqtt_->OnMessage([this](const std::string& topic, const std::string& payload) {
        OnMessage(topic, payload);
    });

    ESP_LOGI(TAG, "Connecting to smart home broker %s:%d", broker_host_.c_str(), broker_port_);
    if (!mqtt_->Connect(broker_host_, broker_port_, client_id_, "", "")) {
        ESP_LOGE(TAG, "Connect failed, code=%d", mqtt_->GetLastError());
        connecting_ = false;
        mqtt_.reset();
        ScheduleReconnect();
    }
}

void SmartHomeClient::SubscribeAll() {
    if (!mqtt_ || !mqtt_->IsConnected()) return;
    const std::string status = Topic("device/+/status");
    const std::string ack = Topic("device/+/ack");
    const std::string event = Topic("event");
    const std::string feedback = Topic("display/feedback");
    mqtt_->Subscribe(status, 0);
    mqtt_->Subscribe(ack, 0);
    mqtt_->Subscribe(event, 0);
    mqtt_->Subscribe(feedback, 0);
    ESP_LOGI(TAG, "Subscribed: %s | %s | %s | %s", status.c_str(), ack.c_str(), event.c_str(),
             feedback.c_str());
}

std::string SmartHomeClient::Topic(const std::string& suffix) const {
    return home_prefix_ + suffix;
}

std::string SmartHomeClient::MakeMsgId() const {
    char buf[40];
    snprintf(buf, sizeof(buf), "dog-%08lx-%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
    return buf;
}

SmartHomeClient::DeviceCache* SmartHomeClient::FindDevice(const std::string& device_id) {
    if (device_id == "outlet_01") return &outlet_01_;
    if (device_id == "outlet_02") return &outlet_02_;
    if (device_id == "gas_alarm_01") return &gas_alarm_01_;
    if (device_id == "temp_humi_01") return &temp_humi_01_;
    return nullptr;
}

void SmartHomeClient::ApplyEntities(DeviceCache& cache, cJSON* entities) {
    if (!cJSON_IsObject(entities)) return;
    auto socket = EntityString(entities, "socket_1");
    if (!socket.empty()) {
        cache.socket_1 = socket;
        cache.state = socket;
    }
    auto gas = EntityString(entities, "gas");
    if (!gas.empty()) cache.gas = gas;
    auto gas_value = EntityString(entities, "gas_value");
    if (!gas_value.empty()) cache.gas_value = gas_value;
    auto temperature = EntityString(entities, "temperature");
    if (!temperature.empty()) cache.temperature = FahrenheitToCelsiusString(temperature);
    auto humidity = EntityString(entities, "humidity");
    if (!humidity.empty()) cache.humidity = humidity;
}

void SmartHomeClient::OnMessage(const std::string& topic, const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Bad JSON on %s", topic.c_str());
        return;
    }

    const std::string prefix = home_prefix_;
    if (topic == Topic("event")) {
        HandleUnifiedEvent(root);
    } else if (topic == Topic("display/feedback")) {
        HandleFeedback(root);
    } else if (topic.size() > prefix.size() && topic.compare(0, prefix.size(), prefix) == 0) {
        const std::string rest = topic.substr(prefix.size());
        if (rest.compare(0, 7, "device/") == 0) {
            const auto slash = rest.find('/', 7);
            if (slash != std::string::npos) {
                const std::string device_id = rest.substr(7, slash - 7);
                const std::string kind = rest.substr(slash + 1);
                if (kind == "status") HandleDeviceStatus(device_id, root);
                else if (kind == "ack") HandleDeviceAck(device_id, root);
            }
        }
    }

    cJSON_Delete(root);
}

void SmartHomeClient::HandleDeviceStatus(const std::string& device_id, cJSON* root) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* cache = FindDevice(device_id);
    if (cache == nullptr) return;
    cache->seen = true;
    cache->online = JsonBool(root, "online", true);
    cache->name = JsonString(root, "name");
    cache->state = JsonString(root, "state");
    cache->ts = JsonInt64(root, "ts");
    ApplyEntities(*cache, cJSON_GetObjectItem(root, "entities"));
    if (cache->socket_1.empty() && !cache->state.empty()) cache->socket_1 = cache->state;
}

void SmartHomeClient::HandleDeviceAck(const std::string& device_id, cJSON* root) {
    auto* entities = cJSON_GetObjectItem(root, "entities");
    if (!cJSON_IsObject(entities)) return;
    auto* success = cJSON_GetObjectItem(root, "success");
    if (cJSON_IsBool(success) && !cJSON_IsTrue(success)) return;

    std::lock_guard<std::mutex> lock(mutex_);
    auto* cache = FindDevice(device_id);
    if (cache == nullptr) return;
    cache->seen = true;
    cache->ts = JsonInt64(root, "ts");
    ApplyEntities(*cache, entities);
}

bool SmartHomeClient::ShouldAnnounce(const std::string& key) {
    if (key.empty()) return true;
    if (key == last_announce_key_) return false;
    last_announce_key_ = key;
    return true;
}

void SmartHomeClient::Announce(const char* status, const std::string& message, const char* emotion,
                               const std::string_view& sound) {
    auto alive = alive_;
    std::string msg = message;
    std::string status_str = status ? status : "告警";
    std::string emotion_str = emotion ? emotion : "surprised";
    Application::GetInstance().Schedule([alive, status_str, msg, emotion_str, sound]() {
        if (!*alive) return;
        Application::GetInstance().Alert(status_str.c_str(), msg.c_str(), emotion_str.c_str(), sound);
    });
}

void SmartHomeClient::HandleUnifiedEvent(cJSON* root) {
    const std::string type = JsonString(root, "type");
    const std::string message = JsonString(root, "message");
    const std::string level = JsonString(root, "level");
    const std::string msg_id = JsonString(root, "msgId");
    const int64_t ts = JsonInt64(root, "ts");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_event_.type = type;
        last_event_.message = message;
        last_event_.level = level;
        last_event_.ts = ts;
    }

    const std::string dedupe =
        !msg_id.empty() ? ("evt:" + msg_id) : ("evt:" + type + ":" + std::to_string(ts));
    bool announce = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        announce = ShouldAnnounce(dedupe);
    }
    if (!announce) return;

    if (type == "dog" || type == "app") {
        Announce("告警", "您已成功发送告警", "happy", Lang::Sounds::OGG_ALERT_SENT);
    } else if (type == "gas") {
        std::string text = "煤气告警：";
        text += message.empty() ? "检测到煤气泄漏" : message;
        text += "，请立即处理";
        Announce("警告", text, "surprised", Lang::Sounds::OGG_GAS_WARNING);
    }
}

void SmartHomeClient::HandleFeedback(cJSON* root) {
    const std::string type = JsonString(root, "type");
    const std::string message = JsonString(root, "message");
    const std::string msg_id = JsonString(root, "msgId");
    const int64_t ts = JsonInt64(root, "ts");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_feedback_.type = type;
        last_feedback_.message = message;
        last_feedback_.ts = ts;
    }

    if (type != "dog_ack" && type != "app_ack" && type != "gas_ack") return;

    const std::string dedupe =
        !msg_id.empty() ? ("fb:" + msg_id) : ("fb:" + type + ":" + std::to_string(ts));
    bool announce = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        announce = ShouldAnnounce(dedupe);
    }
    if (!announce) return;

    Announce("通知", "子女正在赶回来", "happy", Lang::Sounds::OGG_CHILDREN_COMING);
}

bool SmartHomeClient::PublishJson(const std::string& topic, cJSON* root) {
    if (!mqtt_ || !mqtt_->IsConnected()) {
        ESP_LOGW(TAG, "Publish skipped, not connected: %s", topic.c_str());
        return false;
    }
    char* text = cJSON_PrintUnformatted(root);
    if (text == nullptr) return false;
    const bool ok = mqtt_->Publish(topic, text, 0);
    cJSON_free(text);
    return ok;
}

bool SmartHomeClient::SetOutlet(const std::string& device_id, const std::string& action) {
    if (device_id != "outlet_01" && device_id != "outlet_02") return false;
    const std::string mqtt_action = ActionToMqtt(action);
    if (mqtt_action.empty()) return false;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msgId", MakeMsgId().c_str());
    cJSON_AddStringToObject(root, "entity", "socket_1");
    cJSON_AddStringToObject(root, "action", mqtt_action.c_str());
    const bool ok = PublishJson(Topic("device/" + device_id + "/cmd"), root);
    cJSON_Delete(root);
    if (ok) ESP_LOGI(TAG, "Cmd %s %s", device_id.c_str(), mqtt_action.c_str());
    return ok;
}

bool SmartHomeClient::SendSos(const std::string& message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msgId", MakeMsgId().c_str());
    cJSON_AddStringToObject(root, "type", "dog");
    cJSON_AddStringToObject(root, "message",
                            message.empty() ? "小狗发起紧急告警" : message.c_str());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(esp_timer_get_time() / 1000));
    cJSON* data = cJSON_AddObjectToObject(root, "data");
    cJSON_AddStringToObject(data, "source", "dog_robot");
    const bool ok = PublishJson(Topic("device/dog_robot/event"), root);
    cJSON_Delete(root);
    if (ok) ESP_LOGI(TAG, "SOS published");
    return ok;
}

std::string SmartHomeClient::GetStatusJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", IsConnected());
    cJSON_AddStringToObject(root, "broker", (broker_host_ + ":" + std::to_string(broker_port_)).c_str());

    auto add_device = [&](const char* id, const DeviceCache& d) {
        cJSON* obj = cJSON_AddObjectToObject(root, id);
        cJSON_AddBoolToObject(obj, "seen", d.seen);
        cJSON_AddBoolToObject(obj, "online", d.online);
        cJSON_AddStringToObject(obj, "name", d.name.c_str());
        cJSON_AddStringToObject(obj, "state", d.state.c_str());
        cJSON_AddStringToObject(obj, "socket_1", d.socket_1.c_str());
        cJSON_AddStringToObject(obj, "gas", d.gas.c_str());
        cJSON_AddStringToObject(obj, "gas_value", d.gas_value.c_str());
        cJSON_AddStringToObject(obj, "temperature", d.temperature.c_str());
        cJSON_AddStringToObject(obj, "humidity", d.humidity.c_str());
        cJSON_AddNumberToObject(obj, "ts", static_cast<double>(d.ts));
    };
    add_device("outlet_01", outlet_01_);
    add_device("outlet_02", outlet_02_);
    add_device("gas_alarm_01", gas_alarm_01_);
    add_device("temp_humi_01", temp_humi_01_);

    cJSON* event = cJSON_AddObjectToObject(root, "last_event");
    cJSON_AddStringToObject(event, "type", last_event_.type.c_str());
    cJSON_AddStringToObject(event, "message", last_event_.message.c_str());
    cJSON_AddStringToObject(event, "level", last_event_.level.c_str());
    cJSON_AddNumberToObject(event, "ts", static_cast<double>(last_event_.ts));

    cJSON* feedback = cJSON_AddObjectToObject(root, "last_feedback");
    cJSON_AddStringToObject(feedback, "type", last_feedback_.type.c_str());
    cJSON_AddStringToObject(feedback, "message", last_feedback_.message.c_str());
    cJSON_AddNumberToObject(feedback, "ts", static_cast<double>(last_feedback_.ts));

    char* text = cJSON_PrintUnformatted(root);
    std::string result = text == nullptr ? "{}" : text;
    if (text != nullptr) cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

std::string SmartHomeClient::GetStatusSummaryZh() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    out += IsConnected() ? "智能家居已连接。" : "智能家居未连接。";
    out += "风扇（outlet_01）：";
    out += outlet_01_.seen ? (OnOffZh(outlet_01_.socket_1.empty() ? outlet_01_.state : outlet_01_.socket_1) +
                              (outlet_01_.online ? "" : "（离线）"))
                           : "暂无数据";
    out += "。房间灯（outlet_02）：";
    out += outlet_02_.seen ? (OnOffZh(outlet_02_.socket_1.empty() ? outlet_02_.state : outlet_02_.socket_1) +
                              (outlet_02_.online ? "" : "（离线）"))
                           : "暂无数据";
    out += "。燃气：";
    if (!gas_alarm_01_.seen) {
        out += "暂无数据";
    } else {
        out += gas_alarm_01_.gas.empty() ? "未知" : gas_alarm_01_.gas;
        if (!gas_alarm_01_.gas_value.empty()) {
            out += "，浓度 ";
            out += gas_alarm_01_.gas_value;
        }
        if (!gas_alarm_01_.online) out += "（离线）";
    }
    out += "。温湿度：";
    if (!temp_humi_01_.seen) {
        out += "暂无数据";
    } else {
        out += temp_humi_01_.temperature.empty() ? "?" : temp_humi_01_.temperature;
        out += "℃ / ";
        out += temp_humi_01_.humidity.empty() ? "?" : temp_humi_01_.humidity;
        out += "%";
        if (!temp_humi_01_.online) out += "（离线）";
    }
    out += "。";
    return out;
}

void SmartHomeClient::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.smarthome.get_status",
        "查询家里智能设备状态：风扇、房间灯、燃气报警、温湿度。返回中文摘要供播报。",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue { return GetStatusSummaryZh(); });

    mcp.AddTool(
        "self.smarthome.set_fan",
        "控制家里的风扇（智能插座 outlet_01）。用户说开风扇/关风扇/切换风扇时调用。"
        "action 取值：on、off、toggle。",
        PropertyList({Property("action", kPropertyTypeString, "toggle")}),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto action = properties["action"].value<std::string>();
            if (!SetOutlet("outlet_01", action)) {
                return "风扇控制失败：未连接或参数无效（action 需为 on/off/toggle）";
            }
            return true;
        });

    mcp.AddTool(
        "self.smarthome.set_room_light",
        "控制家里的房间灯（智能插座 outlet_02）。"
        "仅当用户明确说「房间的灯」或「房间灯」时调用；"
        "用户只说「开灯」「关灯」而未提房间时不要调用本工具，应使用小狗身上的 self.lamp 彩灯工具。"
        "action 取值：on、off、toggle。",
        PropertyList({Property("action", kPropertyTypeString, "toggle")}),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto action = properties["action"].value<std::string>();
            if (!SetOutlet("outlet_02", action)) {
                return "房间灯控制失败：未连接或参数无效（action 需为 on/off/toggle）";
            }
            return true;
        });

    mcp.AddTool(
        "self.smarthome.sos",
        "发送小狗紧急告警（SOS）到子女端。用户说求救、告警、帮我叫人时调用。可选自定义 message。",
        PropertyList({Property("message", kPropertyTypeString, "")}),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto message = properties["message"].value<std::string>();
            if (!SendSos(message)) return "告警发送失败：智能家居 MQTT 未连接";
            return true;
        });

    ESP_LOGI(TAG, "Smart home MCP tools registered");
}
