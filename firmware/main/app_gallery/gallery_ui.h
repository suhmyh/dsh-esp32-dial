/*
 * Gallery UI - Grid thumbnails + fullscreen animated/static playback
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "lvgl.h"
#include "bsp/esp32_s3_touch_lcd_1_85B.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File type for gallery items
 */
typedef enum {
    GALLERY_FILE_AAF = 0,  /* Animated AAF */
    GALLERY_FILE_PNG,      /* Static PNG */
    GALLERY_FILE_JPG,      /* Static JPG */
} gallery_file_type_t;

/**
 * @brief Gallery file entry (replaces plain string list)
 */
typedef struct {
    char *path;                  /* Full file path */
    char *name;                  /* Display name (without extension) */
    gallery_file_type_t type;    /* File type */
} gallery_file_entry_t;

/**
 * @brief Gallery file list container
 */
typedef struct {
    gallery_file_entry_t *entries;
    int count;
} gallery_file_list_t;

/**
 * @brief Allocate and populate a gallery file list from SD card.
 *        Scans .aaf, .png, .jpg files in the given directory.
 *
 * @param dir_path   SD card directory path (e.g. "/sdcard")
 * @param out        Output list (caller must free with gallery_file_list_free)
 * @return ESP_OK on success
 */
esp_err_t gallery_file_list_init(const char *dir_path, gallery_file_list_t *out);

/**
 * @brief Free a gallery file list
 */
void gallery_file_list_free(gallery_file_list_t *list);

/**
 * @brief Initialize the gallery UI screen with grid thumbnails
 *
 * @param parent     Parent object (usually the app's active screen)
 * @param file_list  List of gallery files
 */
void gallery_ui_init(lv_obj_t *parent, gallery_file_list_t *file_list);

#ifdef __cplusplus
}
#endif
