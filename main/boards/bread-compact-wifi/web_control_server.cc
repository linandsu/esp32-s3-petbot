#include "web_control_server.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "device_state_machine.h"
#include "display.h"
#include "dog_controller.h"
#include "rgb_lamp_controller.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <mdns.h>

#include <cstdlib>
#include <cstring>

static const char* TAG = "DogWebControl";

extern const uint8_t web_control_html_start[] asm("_binary_web_control_html_start");
extern const uint8_t web_control_html_end[] asm("_binary_web_control_html_end");

WebControlServer& WebControlServer::GetInstance() {
    static WebControlServer instance;
    return instance;
}

WebControlServer::WebControlServer() {
    esp_timer_create_args_t args = {
        .callback = &WebControlServer::StatusTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_web_status",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &status_timer_));
}

WebControlServer::~WebControlServer() {
    Stop();
    if (status_timer_ != nullptr) esp_timer_delete(status_timer_);
}

bool WebControlServer::Start() {
    if (server_ != nullptr) return true;

    if (!mdns_started_) {
        if (mdns_init() == ESP_OK) {
            mdns_hostname_set("xiaozhi-dog");
            mdns_instance_name_set("Xiaozhi Dog");
            mdns_started_ = true;
        } else {
            ESP_LOGW(TAG, "mDNS unavailable; use the IP address shown on screen");
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        server_ = nullptr;
        ESP_LOGE(TAG, "Unable to start local web server");
        return false;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = RootHandler, .user_ctx = this};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = StatusHandler, .user_ctx = this};
    httpd_uri_t ws = {.uri = "/api/ws", .method = HTTP_GET, .handler = WsHandler, .user_ctx = this, .is_websocket = true};
    httpd_register_uri_handler(server_, &root);
    httpd_register_uri_handler(server_, &status);
    httpd_register_uri_handler(server_, &ws);
    ESP_ERROR_CHECK(esp_timer_start_periodic(status_timer_, 2000000));
    ESP_LOGI(TAG, "Local control server started on port 80");
    return true;
}

void WebControlServer::Stop() {
    if (status_timer_ != nullptr && esp_timer_is_active(status_timer_)) esp_timer_stop(status_timer_);
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    clients_.clear();
}

std::string WebControlServer::GetControlUrl() const {
    esp_netif_ip_info_t ip_info = {};
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return "";
    char ip[16];
    esp_ip4addr_ntoa(&ip_info.ip, ip, sizeof(ip));
    return std::string("http://") + ip + "/";
}

esp_err_t WebControlServer::RootHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, reinterpret_cast<const char*>(web_control_html_start),
                           web_control_html_end - web_control_html_start);
}

esp_err_t WebControlServer::StatusHandler(httpd_req_t* req) {
    auto* server = static_cast<WebControlServer*>(req->user_ctx);
    httpd_resp_set_type(req, "application/json");
    const auto status = server->BuildStatusJson();
    return httpd_resp_sendstr(req, status.c_str());
}

esp_err_t WebControlServer::WsHandler(httpd_req_t* req) {
    auto& server = GetInstance();
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        server.clients_.insert(fd);
        server.SendWs(fd, "{\"type\":\"state\",\"payload\":" + server.BuildStatusJson() + "}");
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        server.clients_.erase(fd);
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0 || frame.len > 1024) return ESP_ERR_INVALID_ARG;
    char* payload = static_cast<char*>(calloc(1, frame.len + 1));
    if (payload == nullptr) return ESP_ERR_NO_MEM;
    frame.payload = reinterpret_cast<uint8_t*>(payload);
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK) server.HandleCommand(req, payload, frame.len);
    free(payload);
    return err;
}

void WebControlServer::StatusTimerCallback(void* arg) {
    static_cast<WebControlServer*>(arg)->BroadcastStatus();
}

void WebControlServer::SendWs(int fd, const std::string& payload) {
    if (server_ == nullptr) return;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data()));
    frame.len = payload.size();
    if (httpd_ws_send_frame_async(server_, fd, &frame) != ESP_OK) clients_.erase(fd);
}

void WebControlServer::BroadcastStatus() {
    if (server_ == nullptr || clients_.empty()) return;
    const std::string message = "{\"type\":\"state\",\"payload\":" + BuildStatusJson() + "}";
    const auto clients = clients_;
    for (int fd : clients) SendWs(fd, message);
}

std::string WebControlServer::BuildStatusJson() const {
    cJSON* root = cJSON_Parse(Board::GetInstance().GetDeviceStatusJson().c_str());
    if (root == nullptr) root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_state", DeviceStateMachine::GetStateName(Application::GetInstance().GetDeviceState()));
    cJSON_AddStringToObject(root, "listening_mode", Application::GetInstance().GetListeningModeName());
    cJSON_AddBoolToObject(root, "audio_channel_open", Application::GetInstance().IsAudioChannelOpened());
    cJSON_AddBoolToObject(root, "voice_processing", Application::GetInstance().IsVoiceProcessingActive());
    if (auto* dog = DogController::GetInstance()) cJSON_AddStringToObject(root, "dog_action", dog->GetCurrentAction().c_str());
    if (auto* lamp = RgbLampController::GetInstance()) {
        cJSON* lamp_json = cJSON_Parse(lamp->GetStatusJson().c_str());
        cJSON_AddItemToObject(root, "lamp", lamp_json == nullptr ? cJSON_CreateObject() : lamp_json);
    }
    const auto url = GetControlUrl();
    if (!url.empty()) cJSON_AddStringToObject(root, "control_url", url.c_str());
    char* text = cJSON_PrintUnformatted(root);
    std::string result = text == nullptr ? "{}" : text;
    if (text != nullptr) cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

void WebControlServer::HandleCommand(httpd_req_t* req, const char* payload, size_t len) {
    cJSON* root = cJSON_ParseWithLength(payload, len);
    const int fd = httpd_req_to_sockfd(req);
    bool ok = false;
    bool broadcast_on_success = true;
    const char* error = "invalid command";
    int id = 0;
    if (root != nullptr) {
        if (auto* id_item = cJSON_GetObjectItem(root, "id"); cJSON_IsNumber(id_item)) id = id_item->valueint;
        auto* command = cJSON_GetObjectItem(root, "command");
        auto* args = cJSON_GetObjectItem(root, "args");
        const char* name = cJSON_IsString(command) ? command->valuestring : "";
        if (strcmp(name, "dog.action") == 0 && DogController::GetInstance() && args) {
            auto* action = cJSON_GetObjectItem(args, "action");
            ok = cJSON_IsString(action) && DogController::GetInstance()->ExecuteAction(action->valuestring);
            error = "unsupported dog action";
        } else if (strcmp(name, "lamp.set") == 0 && RgbLampController::GetInstance() && args) {
            auto* r_item = cJSON_GetObjectItem(args, "r");
            auto* g_item = cJSON_GetObjectItem(args, "g");
            auto* b_item = cJSON_GetObjectItem(args, "b");
            auto* brightness_item = cJSON_GetObjectItem(args, "brightness");
            int r = cJSON_IsNumber(r_item) ? r_item->valueint : -1;
            int g = cJSON_IsNumber(g_item) ? g_item->valueint : -1;
            int b = cJSON_IsNumber(b_item) ? b_item->valueint : -1;
            int brightness = cJSON_IsNumber(brightness_item) ? brightness_item->valueint : -1;
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255 && brightness >= 0 && brightness <= 255) {
                RgbLampController::GetInstance()->SetColor(r, g, b, brightness);
                ok = true;
            }
            error = "invalid lamp color";
        } else if (strcmp(name, "lamp.update") == 0 && RgbLampController::GetInstance() && args) {
            // Slider/color-picker updates are a high-frequency stream. The
            // requester already has the new local value, and the periodic
            // status broadcast will reconcile other clients. Broadcasting a
            // full status frame for every slider step creates a command queue.
            broadcast_on_success = false;
            auto* r_item = cJSON_GetObjectItem(args, "r");
            auto* g_item = cJSON_GetObjectItem(args, "g");
            auto* b_item = cJSON_GetObjectItem(args, "b");
            auto* brightness_item = cJSON_GetObjectItem(args, "brightness");
            const bool has_color = cJSON_IsNumber(r_item) && cJSON_IsNumber(g_item) && cJSON_IsNumber(b_item);
            const bool has_brightness = cJSON_IsNumber(brightness_item);
            const bool color_valid = !has_color ||
                (r_item->valueint >= 0 && r_item->valueint <= 255 &&
                 g_item->valueint >= 0 && g_item->valueint <= 255 &&
                 b_item->valueint >= 0 && b_item->valueint <= 255);
            const bool brightness_valid = !has_brightness ||
                (brightness_item->valueint >= 0 && brightness_item->valueint <= 255);
            if ((has_color || has_brightness) && color_valid && brightness_valid) {
                if (has_color) {
                    RgbLampController::GetInstance()->UpdateColor(
                        r_item->valueint, g_item->valueint, b_item->valueint);
                }
                if (has_brightness) {
                    RgbLampController::GetInstance()->SetBrightness(brightness_item->valueint);
                }
                ok = true;
            }
            error = "invalid lamp update";
        } else if (strcmp(name, "lamp.effect") == 0 && RgbLampController::GetInstance() && args) {
            auto* effect = cJSON_GetObjectItem(args, "effect");
            ok = cJSON_IsString(effect) && RgbLampController::GetInstance()->SetEffect(effect->valuestring);
            error = "unsupported lamp effect";
        } else if (strcmp(name, "emotion.set") == 0 && args) {
            auto* emotion = cJSON_GetObjectItem(args, "emotion");
            if (auto* display = Board::GetInstance().GetDisplay(); display && cJSON_IsString(emotion)) {
                display->ShowDogFace();
                display->SetEmotion(emotion->valuestring);
                ok = true;
            }
            error = "invalid emotion";
        } else if (strcmp(name, "emotion.auto") == 0) {
            if (auto* display = Board::GetInstance().GetDisplay()) {
                if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) display->ShowDogSleepFace();
                else display->ShowDogFace();
                ok = true;
            }
        } else if (strcmp(name, "volume.set") == 0 && args) {
            auto* volume = cJSON_GetObjectItem(args, "volume");
            if (auto* codec = Board::GetInstance().GetAudioCodec(); codec && cJSON_IsNumber(volume) && volume->valueint >= 0 && volume->valueint <= 100) {
                codec->SetOutputVolume(volume->valueint);
                ok = true;
            }
            error = "invalid volume";
        } else if (strcmp(name, "device.sleep") == 0) {
            Application::GetInstance().EnterSleepMode();
            ok = true;
        } else if (strcmp(name, "device.wake") == 0) {
            Application::GetInstance().WakeForWebControl();
            ok = true;
        }
    }
    if (root != nullptr) cJSON_Delete(root);
    char reply[128];
    snprintf(reply, sizeof(reply), "{\"type\":\"result\",\"id\":%d,\"ok\":%s%s%s%s}", id, ok ? "true" : "false",
             ok ? "" : ",\"error\":\"", ok ? "" : error, ok ? "" : "\"");
    SendWs(fd, reply);
    if (ok && broadcast_on_success) BroadcastStatus();
}
