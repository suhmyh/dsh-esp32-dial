/*
 * SettingsApp - Wrapper for sys_msg_ui as an ESP-Brookesia Phone App
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "systems/phone/assets/esp_brookesia_phone_assets.h"

namespace esp_brookesia::apps {

class SettingsApp: public systems::phone::App {
public:
    static SettingsApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~SettingsApp();

protected:
    SettingsApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static SettingsApp *_instance;
};

} // namespace esp_brookesia::apps
