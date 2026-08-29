/* Xiaolan: the approved v2 pet, permanently visible on the desktop launcher. */
#include "xiaolan_widget.hpp"

#include "esp_log.h"
#include "esp_brookesia.hpp"
#include "assets/xiaolan_assets.hpp"

namespace {
constexpr char TAG[] = "Xiaolan";
constexpr uint32_t kFrameMs = 140;
}

namespace esp_brookesia::apps {

bool XiaolanWidget::begin(lv_obj_t *parent)
{
    if (!parent || _image) return false;
    _image = lv_image_create(parent);
    if (!_image) return false;
    lv_obj_set_size(_image, 128, 139);
    lv_obj_set_pos(_image, 220, 178);
    lv_image_set_antialias(_image, false);
    lv_image_set_src(_image, &xiaolan_idle[0]);
    lv_obj_add_event_cb(_image, onTouch, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_image, onTouch, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_image, onTouch, LV_EVENT_CLICKED, this);
    _timer = lv_timer_create(onTick, kFrameMs, this);
    ESP_LOGI(TAG, "desktop pet ready");
    return _timer != nullptr;
}

void XiaolanWidget::onTick(lv_timer_t *timer)
{
    auto *self = static_cast<XiaolanWidget *>(timer ? timer->user_data : nullptr);
    if (!self || !self->_image) return;
    self->_frame = (self->_frame + 1) % XIAOLAN_IDLE_FRAMES;
    lv_image_set_src(self->_image, &xiaolan_idle[self->_frame]);
}

void XiaolanWidget::onTouch(lv_event_t *event)
{
    auto *self = static_cast<XiaolanWidget *>(lv_event_get_user_data(event));
    if (!self || !self->_image) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (!self->_dragging) {
            self->_dragging = true;
            lv_area_t area;
            lv_obj_get_coords(self->_image, &area);
            self->_drag_offset.x = point.x - area.x1;
            self->_drag_offset.y = point.y - area.y1;
        }
        int x = point.x - self->_drag_offset.x;
        int y = point.y - self->_drag_offset.y;
        if (x < 0) x = 0;
        if (y < 34) y = 34;
        if (x > 232) x = 232;
        if (y > 215) y = 215;
        lv_obj_set_pos(self->_image, x, y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
        self->_dragging = false;
    }
}

} // namespace esp_brookesia::apps
