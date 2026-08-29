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
    constexpr lv_coord_t image_width = 128;
    constexpr lv_coord_t image_height = 139;
    lv_obj_set_size(_image, image_width, image_height);
    const lv_coord_t parent_width = lv_obj_get_content_width(parent);
    const lv_coord_t parent_height = lv_obj_get_content_height(parent);
    lv_obj_set_pos(_image,
                   parent_width > image_width ? (parent_width - image_width) / 2 : 0,
                   parent_height > image_height ? (parent_height - image_height) / 2 : 0);
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
        lv_obj_t *parent = lv_obj_get_parent(self->_image);
        const int parent_width = parent ? lv_obj_get_content_width(parent) : 360;
        const int parent_height = parent ? lv_obj_get_content_height(parent) : 360;
        const int image_width = lv_obj_get_width(self->_image);
        const int image_height = lv_obj_get_height(self->_image);
        const int max_x = parent_width > image_width ? parent_width - image_width : 0;
        const int max_y = parent_height > image_height ? parent_height - image_height : 0;
        int x = point.x - self->_drag_offset.x;
        int y = point.y - self->_drag_offset.y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > max_x) x = max_x;
        if (y > max_y) y = max_y;
        lv_obj_set_pos(self->_image, x, y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
        self->_dragging = false;
    }
}

} // namespace esp_brookesia::apps
