/*
 * DSH Phone App plugin.
 *
 * This first cut deliberately owns its own screen. The old DSH dial widgets
 * are not linked into the desktop process; a later transport adapter can feed
 * this app without changing the Waveshare Desktop base or its navigation.
 */
#include "dsh_app.hpp"

#include "lvgl.h"
#include "esp_brookesia.hpp"

using namespace esp_brookesia::systems;

namespace {

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color,
                            lv_coord_t width = LV_SIZE_CONTENT)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(obj, text);
    if (width != LV_SIZE_CONTENT) {
        lv_obj_set_width(obj, width);
    }
    return obj;
}

} // namespace

namespace esp_brookesia::apps {

DshApp *DshApp::_instance = nullptr;

DshApp *DshApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new DshApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

DshApp::DshApp(bool use_status_bar, bool use_navigation_bar):
    // nullptr uses the upstream launcher default icon until the DSH plugin
    // supplies its own LVGL9 asset.
    App("DSH", nullptr, false, use_status_bar, use_navigation_bar)
{
}

bool DshApp::run(void)
{
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10131B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_screen_load(screen);

    lv_obj_t *title = make_label(screen, "DSH", lv_color_hex(0xFFFFFF), 300);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *state = make_label(screen, "Desktop Plugin", lv_color_hex(0x2D98DA), 300);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_14, 0);
    lv_obj_align(state, LV_ALIGN_CENTER, 0, -12);

    lv_obj_t *detail = make_label(screen, "Codex bridge adapter", lv_color_hex(0xA6ADBB), 300);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 20);

    return true;
}

bool DshApp::back(void)
{
    return notifyCoreClosed();
}

} // namespace esp_brookesia::apps
