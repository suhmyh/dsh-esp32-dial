#pragma once

#include "lvgl.h"

namespace esp_brookesia::apps {

/** A lightweight always-on desktop pet hosted by the Phone launcher screen. */
class XiaolanWidget final {
public:
    bool begin(lv_obj_t *parent);

private:
    static void onTouch(lv_event_t *event);
    static void onTick(lv_timer_t *timer);

    lv_obj_t *_image = nullptr;
    lv_timer_t *_timer = nullptr;
    int _frame = 0;
    bool _dragging = false;
    lv_point_t _drag_offset = {};
};

} // namespace esp_brookesia::apps
