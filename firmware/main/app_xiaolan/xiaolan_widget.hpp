#pragma once

#include "lvgl.h"
#include "esp_websocket_client.h"

namespace esp_brookesia::apps {

/** A lightweight always-on desktop pet hosted by the Phone launcher screen. */
class XiaolanWidget final {
public:
    bool begin(lv_obj_t *parent);
    void setState(const char *phase);

private:
    static void onTouch(lv_event_t *event);
    static void onTick(lv_timer_t *timer);
    static void websocketEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    void loadBridgeConfig();
    void startTransport();
    void handleMessage(const char *text, size_t length);
    void selectAnimation(const char *phase);

    lv_obj_t *_image = nullptr;
    lv_timer_t *_timer = nullptr;
    int _frame = 0;
    const lv_image_dsc_t *_frames = nullptr;
    int _frame_count = 0;
    int _image_width = 160;
    int _image_height = 173;
    bool _dragging = false;
    lv_point_t _drag_offset = {};

    esp_websocket_client_handle_t _ws = nullptr;
    char _uri[256] = {};
    bool _connected = false;
    uint32_t _last_frame_ms = 0;
};

} // namespace esp_brookesia::apps
