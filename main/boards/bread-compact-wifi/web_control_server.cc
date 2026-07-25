#include "web_control_server.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "device_state_machine.h"
#include "display.h"
#include "dog_controller.h"
#include "rgb_lamp_controller.h"
#include "battery_monitor.h"
#include "wake_word_config.h"
#include "smart_home_client.h"

#include <cJSON.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <mdns.h>

#include <cstdlib>
#include <cstring>

static const char* TAG = "DogWebControl";

extern const uint8_t web_control_html_start[] asm("_binary_web_control_html_start");
extern const uint8_t web_control_html_end[] asm("_binary_web_control_html_end");
extern const uint8_t pinyin_pro_min_js_start[] asm("_binary_pinyin_pro_min_js_start");
extern const uint8_t pinyin_pro_min_js_end[] asm("_binary_pinyin_pro_min_js_end");
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");
extern const uint8_t server_key_pem_start[] asm("_binary_server_key_pem_start");
extern const uint8_t server_key_pem_end[] asm("_binary_server_key_pem_end");

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
    if (server_ != nullptr || ssl_server_ != nullptr) return true;

    if (!mdns_started_) {
        if (mdns_init() == ESP_OK) {
            mdns_hostname_set("xiaozhi-dog");
            mdns_instance_name_set("Xiaozhi Dog");
            mdns_started_ = true;
        } else {
            ESP_LOGW(TAG, "mDNS unavailable; use the IP address shown on screen");
        }
    }

    const bool http_ok = StartHttp();
    const bool https_ok = StartHttps();
    if (!http_ok && !https_ok) {
        ESP_LOGE(TAG, "Unable to start local web server");
        return false;
    }

    ESP_ERROR_CHECK(esp_timer_start_periodic(status_timer_, 2000000));
    ESP_LOGI(TAG, "Local control server ready (http=%d https=%d)", (int)http_ok, (int)https_ok);
    return true;
}

void WebControlServer::RegisterHandlers(httpd_handle_t server) {
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = RootHandler, .user_ctx = this};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = StatusHandler, .user_ctx = this};
    httpd_uri_t pinyin = {.uri = "/vendor/pinyin-pro.min.js", .method = HTTP_GET, .handler = PinyinHandler, .user_ctx = this};
    httpd_uri_t ws = {.uri = "/api/ws", .method = HTTP_GET, .handler = WsHandler, .user_ctx = this, .is_websocket = true};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &pinyin);
    httpd_register_uri_handler(server, &ws);
}

bool WebControlServer::StartHttp() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        server_ = nullptr;
        ESP_LOGW(TAG, "HTTP server failed to start on port 80");
        return false;
    }
    RegisterHandlers(server_);
    ESP_LOGI(TAG, "HTTP control server on port 80");
    return true;
}

bool WebControlServer::StartHttps() {
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    // Keep SSL socket count low: each TLS session needs ~40KB internal RAM.
    config.port_secure = 443;
    config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    config.httpd.max_open_sockets = 2;
    config.httpd.lru_purge_enable = true;
    config.httpd.stack_size = 12288;
    config.servercert = server_cert_pem_start;
    config.servercert_len = server_cert_pem_end - server_cert_pem_start;
    config.prvtkey_pem = server_key_pem_start;
    config.prvtkey_len = server_key_pem_end - server_key_pem_start;

    const esp_err_t err = httpd_ssl_start(&ssl_server_, &config);
    if (err != ESP_OK) {
        ssl_server_ = nullptr;
        ESP_LOGW(TAG, "HTTPS server failed on 443: %s (free_heap=%u)",
                 esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
        return false;
    }
    RegisterHandlers(ssl_server_);
    ESP_LOGI(TAG, "HTTPS control server on port 443 (self-signed; use Chrome for gyro)");
    return true;
}

void WebControlServer::Stop() {
    if (status_timer_ != nullptr && esp_timer_is_active(status_timer_)) esp_timer_stop(status_timer_);
    if (ssl_server_ != nullptr) {
        httpd_ssl_stop(ssl_server_);
        ssl_server_ = nullptr;
    }
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
    // Prefer HTTPS so phone browsers can unlock motion sensors.
    if (ssl_server_ != nullptr) return std::string("https://") + ip + "/";
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

esp_err_t WebControlServer::PinyinHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, reinterpret_cast<const char*>(pinyin_pro_min_js_start),
                           pinyin_pro_min_js_end - pinyin_pro_min_js_start);
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

void WebControlServer::RestartTask(void* arg) {
    auto* server = static_cast<WebControlServer*>(arg);
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Applying wake-word configuration and restarting");
    server->restart_pending_ = false;
    esp_restart();
}

void WebControlServer::SendWs(int fd, const std::string& payload) {
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data()));
    frame.len = payload.size();
    if (server_ != nullptr && httpd_ws_send_frame_async(server_, fd, &frame) == ESP_OK) return;
    if (ssl_server_ != nullptr && httpd_ws_send_frame_async(ssl_server_, fd, &frame) == ESP_OK) return;
    clients_.erase(fd);
}

void WebControlServer::BroadcastStatus() {
    if ((server_ == nullptr && ssl_server_ == nullptr) || clients_.empty()) return;
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
    if (auto* dog = DogController::GetInstance()) {
        const auto action = dog->GetCurrentAction();
        cJSON_AddStringToObject(root, "dog_action", action.c_str());
        cJSON_AddBoolToObject(root, "dog_action_running", dog->IsActionRunning());
    }
    if (auto* lamp = RgbLampController::GetInstance()) {
        cJSON* lamp_json = cJSON_Parse(lamp->GetStatusJson().c_str());
        cJSON_AddItemToObject(root, "lamp", lamp_json == nullptr ? cJSON_CreateObject() : lamp_json);
    }
    if (auto* battery = BatteryMonitor::GetInstance()) {
        cJSON* battery_json = cJSON_Parse(battery->GetStatusJson().c_str());
        cJSON_AddItemToObject(root, "battery", battery_json == nullptr ? cJSON_CreateObject() : battery_json);
    }
    if (auto* smarthome = SmartHomeClient::GetInstance()) {
        cJSON* sh_json = cJSON_Parse(smarthome->GetStatusJson().c_str());
        cJSON_AddItemToObject(root, "smarthome", sh_json == nullptr ? cJSON_CreateObject() : sh_json);
    } else {
        cJSON_AddItemToObject(root, "smarthome", cJSON_CreateObject());
    }
    const auto wake_word = WakeWordConfig::GetInstance().GetState();
    cJSON* wake_json = cJSON_AddObjectToObject(root, "wake_word");
    cJSON_AddStringToObject(wake_json, "mode", WakeWordConfig::ModeName(wake_word.mode).c_str());
    cJSON_AddStringToObject(wake_json, "display_text", wake_word.display_text.c_str());
    cJSON_AddStringToObject(wake_json, "preset_model", wake_word.preset_model.c_str());
    cJSON_AddStringToObject(wake_json, "command_pinyin", wake_word.command_pinyin.c_str());
    cJSON_AddNumberToObject(wake_json, "threshold", wake_word.threshold);
    cJSON_AddBoolToObject(wake_json, "fallback", wake_word.fallback);
    cJSON_AddStringToObject(wake_json, "fallback_reason", wake_word.fallback_reason.c_str());
    cJSON_AddBoolToObject(wake_json, "restart_pending", restart_pending_);
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
    bool restart_required = false;
    std::string error = "invalid command";
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
        } else if (strcmp(name, "dog.drive") == 0 && DogController::GetInstance() && args) {
            // High-frequency stream from joystick/gyro; status broadcast is enough.
            broadcast_on_success = false;
            auto* forward_item = cJSON_GetObjectItem(args, "forward");
            auto* turn_item = cJSON_GetObjectItem(args, "turn");
            if (cJSON_IsNumber(forward_item) && cJSON_IsNumber(turn_item)) {
                float forward = static_cast<float>(forward_item->valuedouble);
                float turn = static_cast<float>(turn_item->valuedouble);
                if (forward < -1.f) forward = -1.f;
                if (forward > 1.f) forward = 1.f;
                if (turn < -1.f) turn = -1.f;
                if (turn > 1.f) turn = 1.f;
                ok = DogController::GetInstance()->ExecuteDrive(forward, turn);
            }
            error = "invalid drive command or battery protection active";
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
                ok = RgbLampController::GetInstance()->Configure("color", r, g, b, brightness);
            }
            error = "invalid lamp color or battery protection active";
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
            if ((has_color || has_brightness) && color_valid && brightness_valid &&
                !(BatteryMonitor::GetInstance() && BatteryMonitor::GetInstance()->IsHighLoadBlocked())) {
                if (has_color) {
                    RgbLampController::GetInstance()->UpdateColor(
                        r_item->valueint, g_item->valueint, b_item->valueint);
                }
                if (has_brightness) {
                    RgbLampController::GetInstance()->SetBrightness(brightness_item->valueint);
                }
                ok = true;
            }
            error = "invalid lamp update or battery protection active";
        } else if (strcmp(name, "lamp.effect") == 0 && RgbLampController::GetInstance() && args) {
            auto* effect = cJSON_GetObjectItem(args, "effect");
            ok = cJSON_IsString(effect) && RgbLampController::GetInstance()->SetEffect(effect->valuestring);
            error = "unsupported lamp effect";
        } else if (strcmp(name, "lamp.configure") == 0 && RgbLampController::GetInstance() && args) {
            auto* effect = cJSON_GetObjectItem(args, "effect");
            auto* r = cJSON_GetObjectItem(args, "r");
            auto* g = cJSON_GetObjectItem(args, "g");
            auto* b = cJSON_GetObjectItem(args, "b");
            auto* brightness = cJSON_GetObjectItem(args, "brightness");
            if (cJSON_IsString(effect) && cJSON_IsNumber(r) && cJSON_IsNumber(g) &&
                cJSON_IsNumber(b) && cJSON_IsNumber(brightness) &&
                r->valueint >= 0 && r->valueint <= 255 &&
                g->valueint >= 0 && g->valueint <= 255 &&
                b->valueint >= 0 && b->valueint <= 255 &&
                brightness->valueint >= 0 && brightness->valueint <= 255) {
                ok = RgbLampController::GetInstance()->Configure(
                    effect->valuestring, r->valueint, g->valueint, b->valueint,
                    brightness->valueint);
            }
            error = "invalid lamp configuration";
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
        } else if (strcmp(name, "wake_word.set") == 0 && args) {
            auto* mode = cJSON_GetObjectItem(args, "mode");
            if (!cJSON_IsString(mode)) {
                error = "缺少唤醒词模式";
            } else {
                auto* threshold = cJSON_GetObjectItem(args, "threshold");
                if (!cJSON_IsNumber(threshold)) {
                    error = "请设置有效的检测阈值";
                } else if (strcmp(mode->valuestring, "preset") == 0) {
                    auto* model = cJSON_GetObjectItem(args, "preset_model");
                    if (!cJSON_IsString(model)) {
                        error = "请选择预设唤醒词";
                    } else {
                        ok = WakeWordConfig::GetInstance().SavePreset(
                            model->valuestring, threshold->valuedouble, error);
                    }
                } else if (strcmp(mode->valuestring, "custom") == 0) {
                    auto* display = cJSON_GetObjectItem(args, "display_text");
                    auto* pinyin = cJSON_GetObjectItem(args, "command_pinyin");
                    if (!cJSON_IsString(display) || !cJSON_IsString(pinyin)) {
                        error = "请填写中文唤醒词和识别拼音";
                    } else {
                        ok = WakeWordConfig::GetInstance().SaveCustom(
                            display->valuestring, pinyin->valuestring, threshold->valuedouble, error);
                    }
                } else {
                    error = "不支持的唤醒词模式";
                }
            }
            if (ok) {
                restart_required = true;
                if (!restart_pending_) {
                    restart_pending_ = true;
                    if (xTaskCreate(&WebControlServer::RestartTask, "wake_word_restart", 2048,
                                    this, 4, nullptr) != pdPASS) {
                        restart_pending_ = false;
                        ok = false;
                        restart_required = false;
                        error = "配置已保存，但创建重启任务失败，请手动重启设备";
                    }
                }
            }
        } else if (strcmp(name, "smarthome.set") == 0 && SmartHomeClient::GetInstance() && args) {
            auto* device = cJSON_GetObjectItem(args, "device");
            auto* action = cJSON_GetObjectItem(args, "action");
            if (cJSON_IsString(device) && cJSON_IsString(action)) {
                ok = SmartHomeClient::GetInstance()->SetOutlet(device->valuestring, action->valuestring);
            }
            error = "智能家居控制失败（检查 MQTT 连接与参数）";
        } else if (strcmp(name, "smarthome.sos") == 0 && SmartHomeClient::GetInstance()) {
            std::string message;
            if (args) {
                auto* msg = cJSON_GetObjectItem(args, "message");
                if (cJSON_IsString(msg)) message = msg->valuestring;
            }
            ok = SmartHomeClient::GetInstance()->SendSos(message);
            error = "SOS 发送失败（智能家居 MQTT 未连接）";
        }
    }
    if (root != nullptr) cJSON_Delete(root);
    cJSON* reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "type", "result");
    cJSON_AddNumberToObject(reply, "id", id);
    cJSON_AddBoolToObject(reply, "ok", ok);
    if (!ok) cJSON_AddStringToObject(reply, "error", error.c_str());
    if (restart_required) {
        cJSON_AddBoolToObject(reply, "restart_required", true);
        cJSON_AddNumberToObject(reply, "restart_in_ms", 3000);
    }
    char* reply_text = cJSON_PrintUnformatted(reply);
    SendWs(fd, reply_text == nullptr ? "{\"type\":\"result\",\"ok\":false}" : reply_text);
    if (reply_text != nullptr) cJSON_free(reply_text);
    cJSON_Delete(reply);
    if (ok && broadcast_on_success) BroadcastStatus();
}
