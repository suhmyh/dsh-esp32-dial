#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "esp_websocket_client.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class DshApp final : public systems::phone::App {
public:
    static DshApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = false);
    ~DshApp() override = default;

protected:
    DshApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static DshApp *_instance;
    static void websocketEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    void loadBridgeConfig();
    void startTransport();
    void stopTransport();
    void handleMessage(const char *text, size_t length);
    void applyState(void *json);
    void showAsk(void *json);
    void hideAsk();
    void sendAnswer(int index);
    void refreshUi();
    void setConnection(const char *text, uint32_t color);

    lv_obj_t *_screen = nullptr;
    lv_obj_t *_phase = nullptr;
    lv_obj_t *_title = nullptr;
    lv_obj_t *_detail = nullptr;
    lv_obj_t *_context = nullptr;
    lv_obj_t *_connection = nullptr;
    lv_obj_t *_arc = nullptr;
    lv_obj_t *_activities[3] = {};
    lv_obj_t *_ask_panel = nullptr;
    lv_obj_t *_ask_title = nullptr;
    lv_obj_t *_ask_body = nullptr;
    lv_obj_t *_ask_buttons[3] = {};
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
    uint32_t _last_done_ms = 0;
    uint8_t _animation = 0;

    char _phase_name[16] = "OFFLINE";
    char _state_title[64] = "Waiting for DSH bridge";
    char _state_detail[192] = "Open the bridge and configure host/token in NVS.";
    char _state_activities[3][48] = {};
    bool _activity_running[3] = {};
    int _context_percent = 0;

    char _ask_id[48] = {};
    char _ask_option_ids[3][32] = {};
    int _ask_count = 0;
};

} // namespace esp_brookesia::apps
