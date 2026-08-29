/*
 * GalleryApp - Wrapper for mixed media gallery as an ESP-Brookesia Phone App
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Gallery"
#include "esp_lib_utils.h"
#include "gallery_app.hpp"
#include "app_gallery/gallery_ui.h"

#define APP_NAME "Gallery"

LV_IMG_DECLARE(Photos);

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

GalleryApp *GalleryApp::_instance = nullptr;

GalleryApp *GalleryApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new GalleryApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

GalleryApp::GalleryApp(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &Photos, false, use_status_bar, use_navigation_bar),
    _file_list(nullptr)
{
}

GalleryApp::~GalleryApp()
{
}

void GalleryApp::setFileList(gallery_file_list_t *file_list)
{
    _file_list = file_list;
}

bool GalleryApp::run(void)
{
    ESP_UTILS_LOGD("Run");

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_screen_load(screen);

    gallery_ui_init(screen, _file_list);

    return true;
}

bool GalleryApp::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
