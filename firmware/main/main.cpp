/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "esp_ota_ops.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "esp_brookesia.hpp"
#include "iot_button.h"
#include "button_gpio.h"
#include "boost/thread.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
#include "./dark/stylesheet.hpp"
#include "app_msg/submenu_ui/set_wifi_service.h"
#include "app_wrappers/settings_app.hpp"
#include "app_wrappers/music_app.hpp"
#include "app_wrappers/gallery_app.hpp"
#include "app_dsh/dsh_app.hpp"
#include "app_codex/codex_app.hpp"
#include "app_xiaolan/xiaolan_widget.hpp"
#include "app_msg/submenu_ui/set_wifi_service.h"

// C headers
extern "C" {
#include "app_music/lvgl_music.h"
#include "app_msg/settings_ui.h"
#include "wake_word_det/wake_word_drv.h"
}

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;
using namespace esp_brookesia::apps;

#define TAG              "main"

constexpr bool EXAMPLE_SHOW_MEM_INFO = true;

static generic_file_list_t mp3_files;
static gallery_file_list_t gallery_files;

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
    bsp_sdcard_mount();

    /* Initialize BOOT button as power key (toggle backlight) */
    boot_button_init();

    /* Initialize system modules from original ESP32-S3-Touch-LCD-1.85B-desktop */
    Audio_Play_Init();
    settings_driver_init();
    /*
     * The upstream wake-word task assumes the optional `model` partition has
     * been populated. Our factory image does not ship those large models; the
     * old unconditional init therefore dereferences a null AFE/model handle
     * and reboots before the first LVGL frame. Keep the feature compiled for a
     * later plugin, but do not start it until a model-aware app enables it.
     */

    /* Scan mp3 files for music player */
    esp_err_t err = get_file_list_by_ext("/sdcard/music", ".mp3", &mp3_files);
    if (err == ESP_OK) {
        for (int i = 0; i < mp3_files.count; i++) {
            printf("MP3[%d]: %s\n", i, mp3_files.list[i]);
        }
    }

    /* Scan gallery files (aaf/png/jpg) */
    err = gallery_file_list_init("/sdcard/Pictures", &gallery_files);
    if (err == ESP_OK) {
        for (int i = 0; i < gallery_files.count; i++) {
            printf("Gallery[%d]: %s (%s)\n", i, gallery_files.entries[i].path,
                   gallery_files.entries[i].type == GALLERY_FILE_AAF ? "AAF" :
                   gallery_files.entries[i].type == GALLERY_FILE_PNG ? "PNG" : "JPG");
        }
    }

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

        /* Install Settings app */
        auto settingsApp = SettingsApp::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(settingsApp), "Install SettingsApp failed");

        /* Install Music app (lvgl_music) */
        auto musicApp = MusicApp::requestInstance();
        musicApp->setFileList(&mp3_files);
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(musicApp), "Install MusicApp failed");

        /* Install Gallery app */
        auto galleryApp = GalleryApp::requestInstance();
        galleryApp->setFileList(&gallery_files);
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(galleryApp), "Install GalleryApp failed");

        /* DSH is a normal Desktop application. It does not own the shell. */
        auto dshApp = DshApp::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(dshApp), "Install DshApp failed");

        /* Codex is a separate full-screen controller app. */
        auto codexApp = CodexApp::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(codexApp), "Install CodexApp failed");

        /* Xiaolan is a desktop-layer pet, not an app: it stays on the launcher
         * and disappears naturally whenever a full-screen app is opened. */
        ESP_UTILS_CHECK_FALSE_EXIT(
            g_xiaolan.begin(phone->getDisplay().getMainScreenObject()),
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

    if constexpr (EXAMPLE_SHOW_MEM_INFO) {
        esp_utils::thread_config_guard thread_config({
            .name = "mem_info",
            .stack_size = 4096,
        });
        boost::thread([ = ]() {
            char buffer[128];    /* Make sure buffer is enough for `sprintf` */
            size_t internal_free = 0;
            size_t internal_total = 0;
            size_t external_free = 0;
            size_t external_total = 0;

            while (1) 
            {
                internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
                external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
                sprintf(buffer,
                        "\t           Biggest /     Free /    Total\n"
                        "\t  SRAM : [%8d / %8d / %8d]\n"
                        "\t PSRAM : [%8d / %8d / %8d]",
                        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), internal_free, internal_total,
                        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free, external_total);
                
                {
                    LvLockGuard gui_guard;
                    ESP_UTILS_CHECK_FALSE_EXIT(
                        phone->getDisplay().getRecentsScreen()->setMemoryLabel(
                            internal_free / 1024, internal_total / 1024, external_free / 1024, external_total / 1024
                        ), "Set memory label failed"
                    );
                }

                boost::this_thread::sleep_for(boost::chrono::seconds(2));
            }
        }).detach();
    }
}
