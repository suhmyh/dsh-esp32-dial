/*
 * GalleryApp - Wrapper for mixed media gallery as an ESP-Brookesia Phone App
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "app_gallery/gallery_ui.h"

namespace esp_brookesia::apps {

class GalleryApp: public systems::phone::App {
public:
    static GalleryApp *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~GalleryApp();

    void setFileList(gallery_file_list_t *file_list);

protected:
    GalleryApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static GalleryApp *_instance;
    gallery_file_list_t *_file_list;
};

} // namespace esp_brookesia::apps
