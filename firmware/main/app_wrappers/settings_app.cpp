/*
 * SettingsApp - Wrapper for settings_ui as an ESP-Brookesia Phone App
 */
#include "lvgl.h"
#include "esp_wifi.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Settings"
#include "esp_lib_utils.h"
#include "settings_app.hpp"
#include "app_msg/settings_ui.h"
#include "app_msg/submenu_ui/set_wifi_service.h"

#define APP_NAME "Settings"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(Settings);

namespace esp_brookesia::apps {

SettingsApp *SettingsApp::_instance = nullptr;

/* 系统状态栏WiFi图标更新回调：将WiFi事件桥接到Brookesia框架的状态栏 */
static void update_system_status_bar_wifi(int state)
{
    SettingsApp *app = SettingsApp::requestInstance();
    if (app == nullptr) return;

    auto *phone = app->getSystem();
    if (phone == nullptr) return;

    auto *status_bar = phone->getDisplay().getStatusBar();
    if (status_bar == nullptr) return;

    status_bar->setWifiIconState(state);
}

SettingsApp *SettingsApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new SettingsApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

SettingsApp::SettingsApp(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &Settings, false, use_status_bar, use_navigation_bar)
{
}

SettingsApp::~SettingsApp()
{
}

bool SettingsApp::run(void)
{
    ESP_UTILS_LOGD("Run");

    /* 注册系统状态栏WiFi图标更新回调 */
    wifi_register_status_bar_callback(update_system_status_bar_wifi);

    /* 同步当前WiFi状态到系统状态栏（处理App打开前WiFi已连接的情况） */
    {
        auto *phone = getSystem();
        if (phone) {
            auto *status_bar = phone->getDisplay().getStatusBar();
            if (status_bar) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    /* WiFi已连接，根据RSSI设置信号强度 */
                    int level = 0; /* DISCONNECTED */
                    if (ap_info.rssi >= -60) {
                        level = 3; /* SIGNAL_3 */
                    } else if (ap_info.rssi >= -70) {
                        level = 2; /* SIGNAL_2 */
                    } else if (ap_info.rssi >= -80) {
                        level = 1; /* SIGNAL_1 */
                    }
                    status_bar->setWifiIconState(level);
                }
            }
        }
    }

    // Create a new screen for the settings UI
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_screen_load(screen);

    settings_ui_screen_init(screen, screen);

    return true;
}

bool SettingsApp::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
