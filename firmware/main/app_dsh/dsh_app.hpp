/*
 * DSH is an application of the Waveshare Desktop, not the firmware shell.
 * The transport and state renderer will live behind this Phone::App boundary.
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

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
};

} // namespace esp_brookesia::apps
