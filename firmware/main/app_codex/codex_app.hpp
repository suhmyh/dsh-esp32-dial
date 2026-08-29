#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "esp_websocket_client.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class CodexApp final : public systems::phone::App {
public:
    static CodexApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = false);
    ~CodexApp() override = default;

protected:
    CodexApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static CodexApp *_instance;
    static void websocketEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    void loadBridgeConfig();
    void startTransport();
    void stopTransport();
    void handleMessage(const char *text, size_t length);
    void applyState(void *json);
    void refreshUi();
    void selectAgent(int index);
    void setConnection(const char *text, uint32_t color);

    lv_obj_t *_screen = nullptr;
    lv_obj_t *_connection = nullptr;
    lv_obj_t *_arc = nullptr;
    lv_obj_t *_quota = nullptr;
    lv_obj_t *_agent_count = nullptr;
    lv_obj_t *_selected = nullptr;
    lv_obj_t *_detail = nullptr;
    lv_obj_t *_agent_buttons[6] = {};
    lv_obj_t *_agent_labels[6] = {};
    lv_timer_t *_timer = nullptr;

    esp_websocket_client_handle_t _ws = nullptr;
    char _uri[256] = {};
    char _host[96] = {};
    char _token[128] = {};
    uint16_t _port = 7002;
    bool _transport_started = false;
    bool _connected = false;
    uint32_t _last_frame_ms = 0;
    uint32_t _last_ping_ms = 0;
    uint8_t _animation = 0;
    int _selected_index = 0;
    int _context_percent = 0;
    int _running = 0;
    int _sessions = 0;

    char _phase_name[16] = "OFFLINE";
    char _agent_phase[6][16] = {};
    char _agent_detail[6][40] = {};
};

} // namespace esp_brookesia::apps
