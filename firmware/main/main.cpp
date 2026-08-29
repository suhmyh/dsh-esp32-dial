/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_brookesia.hpp"
#include "iot_button.h"
#include "button_gpio.h"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
#include "./dark/stylesheet.hpp"
#include "app_msg/submenu_ui/set_wifi_service.h"
#include "app_codex/codex_app.hpp"
#include "app_xiaolan/xiaolan_widget.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;
using namespace esp_brookesia::apps;

#define TAG              "main"

/* 全局Phone指针，供WiFi状态栏回调使用（在phone->begin()之后设置） */
static Phone *g_phone = nullptr;

/* 背光状态：false=熄灭，true=点亮 */
static bool g_backlight_on = true;
static esp_brookesia::apps::XiaolanWidget g_xiaolan;

/* BOOT按键回调：按一下切换背光 + 触摸（模拟手机电源键） */
static void boot_button_cb(void *arg, void *usr_data)
{
    lv_indev_t *tp = bsp_display_get_input_dev();
    if (g_backlight_on) {
        ESP_LOGI(TAG, "BOOT: Turn off display (backlight + touch)");
        bsp_display_backlight_off();
        if (tp) lv_indev_enable(tp, false);
        g_backlight_on = false;
    } else {
        ESP_LOGI(TAG, "BOOT: Turn on display (backlight + touch)");
        bsp_display_backlight_on();
        if (tp) lv_indev_enable(tp, true);
        g_backlight_on = true;
    }
}

/* 初始化BOOT按键（GPIO0） */
static void boot_button_init(void)
{
    button_config_t btn_cfg = {
        .long_press_time = 1500,
        .short_press_time = 180,
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = GPIO_NUM_0,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t btn_handle = NULL;
    if (iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create BOOT button");
        return;
    }
    iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, NULL, boot_button_cb, NULL);
    ESP_LOGI(TAG, "BOOT button initialized (GPIO0), single-click to toggle backlight");
}

extern "C" void app_main(void)
{
    const esp_partition_t *update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    ESP_LOGI(TAG, "Switch to partition factory");
    esp_ota_set_boot_partition(update_partition);
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");
    lv_display_t *disp = bsp_display_start();
    lv_indev_t *tp = bsp_display_get_input_dev();
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");
    /* Initialize BOOT button as power key (toggle backlight) */
    boot_button_init();
    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        esp_err_t ret = bsp_display_lock(timeout_ms);
        ESP_UTILS_CHECK_FALSE_RETURN(ret == ESP_OK, false, "Lock failed (timeout_ms: %d)", timeout_ms);
        return true;
    }, []() {
        bsp_display_unlock();
        return true;
    });

    /* Create a phone object */
    ESP_LOGI(TAG, "Create phone object");
    Phone *phone = new Phone(disp);
    phone->setTouchDevice(tp);
    
    Stylesheet *stylesheet = new Stylesheet(STYLESHEET_360_360_DARK);
    ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");

    ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
    ESP_UTILS_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");
    ESP_UTILS_CHECK_FALSE_EXIT(phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
    delete stylesheet;

    {
        LvLockGuard gui_guard;

        /* Begin the phone */
        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");

        /* 设置全局Phone指针，并注册WiFi状态栏回调（确保WiFi事件能更新系统状态栏） */
        g_phone = phone;
        wifi_register_status_bar_callback([](int state) {
            if (g_phone) {
                g_phone->getDisplay().getStatusBar()->setWifiIconState(state);
            }
        });

        /* Codex is the only launcher application, placed on page 1. */
        auto codexApp = CodexApp::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(codexApp), "Install CodexApp failed");

        /* Xiaolan owns page 0 of the launcher.  It is a home-page widget, not
         * an application icon, so swiping right exposes the Codex page. */
        auto *launcher = phone->getDisplay().getAppLauncher();
        ESP_UTILS_CHECK_FALSE_EXIT(
            launcher && g_xiaolan.begin(launcher->getPageMainObject(0)),
            "Install Xiaolan desktop pet failed");

        /* Create a timer to update the clock */
        lv_timer_create([](lv_timer_t *t) {
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                "Refresh status bar failed"
            );
            
        }, 1000, phone);
    }

}
