/*
 * MusicApp - Wrapper for lvgl_music as an ESP-Brookesia Phone App
 */
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:MusicApp"
#include "esp_lib_utils.h"
#include "music_app.hpp"
#include "app_music/lvgl_music.h"

#define APP_NAME "Music"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(Music);

namespace esp_brookesia::apps {

MusicApp *MusicApp::_instance = nullptr;

MusicApp *MusicApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new MusicApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

MusicApp::MusicApp(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &Music, false, use_status_bar, use_navigation_bar),
    _file_list(nullptr)
{
}

MusicApp::~MusicApp()
{
}

void MusicApp::setFileList(generic_file_list_t *file_list)
{
    _file_list = file_list;
}

bool MusicApp::run(void)
{
    ESP_UTILS_LOGD("Run");

    // Create a new screen for the music UI
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_screen_load(screen);

    init_music_ui_screen(screen, _file_list);

    return true;
}

bool MusicApp::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
