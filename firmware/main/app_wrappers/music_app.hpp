/*
 * MusicApp - Wrapper for lvgl_music as an ESP-Brookesia Phone App
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "bsp/esp32_s3_touch_lcd_1_85B.h"

namespace esp_brookesia::apps {

class MusicApp: public systems::phone::App {
public:
    static MusicApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~MusicApp();

    void setFileList(generic_file_list_t *file_list);

protected:
    MusicApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static MusicApp *_instance;
    generic_file_list_t *_file_list;
};

} // namespace esp_brookesia::apps
