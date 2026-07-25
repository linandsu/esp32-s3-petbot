#ifndef WEB_CONTROL_SERVER_H
#define WEB_CONTROL_SERVER_H

#include <esp_http_server.h>
#include <esp_timer.h>

#include <set>
#include <string>

class WebControlServer {
public:
    static WebControlServer& GetInstance();

    bool Start();
    void Stop();
    bool IsRunning() const { return server_ != nullptr || ssl_server_ != nullptr; }
    std::string GetControlUrl() const;
    void BroadcastStatus();

private:
    httpd_handle_t server_ = nullptr;
    httpd_handle_t ssl_server_ = nullptr;
    esp_timer_handle_t status_timer_ = nullptr;
    bool restart_pending_ = false;
    bool mdns_started_ = false;
    std::set<int> clients_;

    WebControlServer();
    ~WebControlServer();
    WebControlServer(const WebControlServer&) = delete;
    WebControlServer& operator=(const WebControlServer&) = delete;

    bool StartHttp();
    bool StartHttps();
    void RegisterHandlers(httpd_handle_t server);

    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t StatusHandler(httpd_req_t* req);
    static esp_err_t PinyinHandler(httpd_req_t* req);
    static esp_err_t WsHandler(httpd_req_t* req);
    static void StatusTimerCallback(void* arg);
    static void RestartTask(void* arg);
    void HandleCommand(httpd_req_t* req, const char* payload, size_t len);
    std::string BuildStatusJson() const;
    void SendWs(int fd, const std::string& payload);
};

#endif
