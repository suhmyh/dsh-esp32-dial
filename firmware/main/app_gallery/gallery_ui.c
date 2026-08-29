/*
 * Gallery UI - Grid thumbnails + fullscreen playback (AAF/PNG/JPG)
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "anim_player.h"
#include "gallery_ui.h"
#include <png.h>

static const char *TAG = "GalleryUI";

/* Forward declarations */
static lv_image_dsc_t *load_png_rgb565(const char *lvgl_path, int max_w, int max_h);
static void free_png_rgb565(lv_image_dsc_t *dsc);
static void fullscreen_cleanup(void);

/* ============ SD Card LVGL Filesystem Driver (letter 'S') ============ */

#define SD_CARD_BASE_PATH "/sdcard"
static bool s_sd_fs_registered = false;

static void *sd_fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    char full_path[512];
    snprintf(full_path, sizeof(full_path), SD_CARD_BASE_PATH "%s", path);

    const char *flags;
    if (mode == LV_FS_MODE_WR) {
        flags = "wb";
    } else if (mode == LV_FS_MODE_RD) {
        flags = "rb";
    } else {
        return NULL;
    }

    FILE *fp = fopen(full_path, flags);
    return (void *)fp;
}

static lv_fs_res_t sd_fs_close(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
    if (file_p) fclose((FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
    size_t r = fread(buf, 1, btr, (FILE *)file_p);
    *br = (uint32_t)r;
    if (r == 0 && btr > 0 && ferror((FILE *)file_p)) return LV_FS_RES_FS_ERR;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw)
{
    (void)drv;
    size_t w = fwrite(buf, 1, btw, (FILE *)file_p);
    *bw = (uint32_t)w;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
    int wh;
    switch (whence) {
    case LV_FS_SEEK_SET: wh = SEEK_SET; break;
    case LV_FS_SEEK_CUR: wh = SEEK_CUR; break;
    case LV_FS_SEEK_END: wh = SEEK_END; break;
    default: return LV_FS_RES_INV_PARAM;
    }
    fseek((FILE *)file_p, (long)pos, wh);
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv;
    long p = ftell((FILE *)file_p);
    if (p < 0) return LV_FS_RES_FS_ERR;
    *pos_p = (uint32_t)p;
    return LV_FS_RES_OK;
}

static void sd_fs_register(void)
{
    if (s_sd_fs_registered) return;

    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);
    fs_drv.letter = 'S';
    fs_drv.open_cb = sd_fs_open;
    fs_drv.close_cb = sd_fs_close;
    fs_drv.read_cb = sd_fs_read;
    fs_drv.write_cb = sd_fs_write;
    fs_drv.seek_cb = sd_fs_seek;
    fs_drv.tell_cb = sd_fs_tell;
    lv_fs_drv_register(&fs_drv);

    s_sd_fs_registered = true;
    ESP_LOGI(TAG, "LVGL SD card FS driver registered (drive 'S')");
}

/* Thumbnail grid layout */
#define THUMB_W         100
#define THUMB_H         120
#define THUMB_GAP       10
#define THUMB_IMG_H     80
#define GRID_PAD_X      15

/* Colors for AAF thumbnail placeholders */
static const lv_color_t thumb_colors[] = {
    LV_COLOR_MAKE(0xE0, 0x60, 0x60),
    LV_COLOR_MAKE(0x60, 0xA0, 0xE0),
    LV_COLOR_MAKE(0x60, 0xC0, 0x80),
    LV_COLOR_MAKE(0xE0, 0xC0, 0x40),
    LV_COLOR_MAKE(0xA0, 0x70, 0xD0),
    LV_COLOR_MAKE(0x50, 0xC0, 0xC0),
    LV_COLOR_MAKE(0xE0, 0x90, 0x50),
    LV_COLOR_MAKE(0xC0, 0x60, 0xA0),
};
#define THUMB_COLORS_COUNT (sizeof(thumb_colors) / sizeof(thumb_colors[0]))

/* Mount point for SD card (LVGL filesystem letter prefix) */
#define SD_MOUNT_PREFIX "S:"

/* ============ Gallery File List ============ */

static const char *s_extensions[] = { ".aaf", ".png", ".jpg", ".jpeg", NULL };

static gallery_file_type_t get_file_type(const char *ext)
{
    if (!ext) return GALLERY_FILE_AAF;
    if (strcasecmp(ext, ".aaf") == 0)  return GALLERY_FILE_AAF;
    if (strcasecmp(ext, ".png") == 0)  return GALLERY_FILE_PNG;
    if (strcasecmp(ext, ".jpg") == 0)  return GALLERY_FILE_JPG;
    if (strcasecmp(ext, ".jpeg") == 0) return GALLERY_FILE_JPG;
    return GALLERY_FILE_AAF;
}

static void strip_ext(const char *filename, char *out, size_t out_len)
{
    const char *dot = strrchr(filename, '.');
    size_t len;
    if (dot) {
        len = (size_t)(dot - filename);
    } else {
        len = strlen(filename);
    }
    if (len >= out_len) len = out_len - 1;
    memcpy(out, filename, len);
    out[len] = '\0';
}

esp_err_t gallery_file_list_init(const char *dir_path, gallery_file_list_t *out)
{
    if (!dir_path || !out) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Cannot open dir: %s", dir_path);
        return ESP_FAIL;
    }

    /* First pass: count matching files */
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) continue;
        for (int i = 0; s_extensions[i]; i++) {
            if (strcasecmp(ext, s_extensions[i]) == 0) {
                count++;
                break;
            }
        }
    }

    if (count == 0) {
        closedir(dir);
        return ESP_ERR_NOT_FOUND;
    }

    out->entries = (gallery_file_entry_t *)calloc(count, sizeof(gallery_file_entry_t));
    if (!out->entries) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    /* Second pass: populate entries */
    rewinddir(dir);
    int idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < count) {
        if (entry->d_type != DT_REG) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) continue;

        bool matched = false;
        for (int i = 0; s_extensions[i]; i++) {
            if (strcasecmp(ext, s_extensions[i]) == 0) {
                matched = true;
                break;
            }
        }
        if (!matched) continue;

        /* Full path */
        size_t path_len = strlen(dir_path) + strlen(entry->d_name) + 2;
        char *full_path = (char *)malloc(path_len);
        if (!full_path) continue;
        snprintf(full_path, path_len, "%s/%s", dir_path, entry->d_name);

        /* Display name */
        char display_name[128];
        strip_ext(entry->d_name, display_name, sizeof(display_name));
        char *name_copy = strdup(display_name);

        out->entries[idx].path = full_path;
        out->entries[idx].name = name_copy ? name_copy : full_path;
        out->entries[idx].type = get_file_type(ext);
        idx++;
    }
    out->count = idx;

    closedir(dir);
    ESP_LOGI(TAG, "Found %d gallery files", out->count);
    return ESP_OK;
}

void gallery_file_list_free(gallery_file_list_t *list)
{
    if (!list || !list->entries) return;
    for (int i = 0; i < list->count; i++) {
        if (list->entries[i].path) free(list->entries[i].path);
        if (list->entries[i].name) free(list->entries[i].name);
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
}

/* ============ Fullscreen Player/Viewer Context ============ */

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *canvas;
    lv_obj_t *image;
    lv_obj_t *label_name;
    lv_timer_t *refresh_timer;
    lv_timer_t *defer_timer;       /* Timer for deferred image decode */
    anim_player_handle_t player_handle;
    gallery_file_type_t file_type;
    bool player_running;
    int canvas_w;
    int canvas_h;
    int aaf_video_w;             /* AAF video width from header */
    int aaf_video_h;             /* AAF video height from header */
    int img_disp_w;              /* PNG/JPG display width */
    int img_disp_h;              /* PNG/JPG display height */
    char lvgl_path[256];         /* LVGL FS path for deferred decode */
    lv_obj_t *loading_label;     /* "Loading..." spinner */
    lv_image_dsc_t *png_dsc;     /* Fast PNG decoded data (for cleanup) */
    lv_obj_t *prev_screen;        /* Screen to restore on close */
} fullscreen_ctx_t;

static fullscreen_ctx_t *g_fs = NULL;

static void fullscreen_cleanup(void);

/* AAF flush callback — canvas is sized to match video, so no offset needed */
static void anim_flush_cb(anim_player_handle_t handle, int x1, int y1, int x2, int y2, const void *data)
{
    fullscreen_ctx_t *ctx = (fullscreen_ctx_t *)anim_player_get_user_data(handle);
    if (!ctx || !ctx->canvas) {
        anim_player_flush_ready(handle);
        return;
    }

    int w = x2 - x1;
    int h = y2 - y1;
    if (w <= 0 || h <= 0) {
        anim_player_flush_ready(handle);
        return;
    }

    lv_area_t dst_area;
    dst_area.x1 = (lv_coord_t)x1;
    dst_area.y1 = (lv_coord_t)y1;
    dst_area.x2 = (lv_coord_t)(x1 + w - 1);
    dst_area.y2 = (lv_coord_t)(y1 + h - 1);
    dst_area.x2 = (lv_coord_t)(x1 + w - 1);
    dst_area.y2 = (lv_coord_t)(y1 + h - 1);

    lv_draw_buf_t src_buf;
    lv_draw_buf_init(&src_buf, (uint32_t)w, (uint32_t)h, LV_COLOR_FORMAT_RGB565,
                     w * sizeof(uint16_t), (void *)data, w * h * sizeof(uint16_t));

    lv_area_t src_area;
    src_area.x1 = 0;
    src_area.y1 = 0;
    src_area.x2 = (lv_coord_t)(w - 1);
    src_area.y2 = (lv_coord_t)(h - 1);

    lv_canvas_copy_buf(ctx->canvas, &dst_area, &src_buf, &src_area);
    anim_player_flush_ready(handle);
}

static void anim_update_cb(anim_player_handle_t handle, player_event_t event)
{
    (void)handle;
    (void)event;
}

static void canvas_refresh_timer_cb(lv_timer_t *t)
{
    fullscreen_ctx_t *ctx = (fullscreen_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx || !ctx->canvas) return;
    lv_obj_invalidate(ctx->canvas);
}

/* Deferred PNG/JPG decode: called after screen with spinner is visible */
static void defer_image_decode_cb(lv_timer_t *t)
{
    fullscreen_ctx_t *ctx = (fullscreen_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx) return;

    ctx->defer_timer = NULL;

    /* Remove loading spinner */
    if (ctx->loading_label) {
        lv_obj_del(ctx->loading_label);
        ctx->loading_label = NULL;
    }

    /* Fast PNG decode: RGB565 direct, no ARGB8888 overhead */
    ctx->png_dsc = load_png_rgb565(ctx->lvgl_path, 260, 260);
    if (!ctx->png_dsc) {
        ESP_LOGE(TAG, "Failed to decode: %s", ctx->lvgl_path);
        return;
    }

    /* Create image widget with pre-decoded data */
    ctx->image = lv_image_create(ctx->screen);
    lv_obj_set_size(ctx->image, (int32_t)ctx->png_dsc->header.w,
                    (int32_t)ctx->png_dsc->header.h);
    lv_obj_align(ctx->image, LV_ALIGN_CENTER, 0, -4);
    lv_image_set_antialias(ctx->image, true);
    lv_image_set_src(ctx->image, ctx->png_dsc);

    ESP_LOGI(TAG, "Fast decode done: %s (%dx%d)",
             ctx->lvgl_path,
             (int)ctx->png_dsc->header.w,
             (int)ctx->png_dsc->header.h);
}

static void fullscreen_cleanup(void)
{
    if (!g_fs) return;

    if (g_fs->refresh_timer) {
        lv_timer_del(g_fs->refresh_timer);
        g_fs->refresh_timer = NULL;
    }

    if (g_fs->defer_timer) {
        lv_timer_del(g_fs->defer_timer);
        g_fs->defer_timer = NULL;
    }

    if (g_fs->player_handle) {
        anim_player_update(g_fs->player_handle, PLAYER_ACTION_STOP);
        vTaskDelay(pdMS_TO_TICKS(150));
        anim_player_deinit(g_fs->player_handle);
        g_fs->player_handle = NULL;
    }

    /* Restore previous screen BEFORE deleting the fullscreen screen.
     * This ensures LVGL always has a valid active screen after deletion. */
    if (g_fs->prev_screen) {
        lv_screen_load(g_fs->prev_screen);
    }

    if (g_fs->screen) {
        lv_obj_del(g_fs->screen);
        g_fs->screen = NULL;
    }

    /* Free fast-PNG decoded data if any */
    if (g_fs->png_dsc) {
        free_png_rgb565(g_fs->png_dsc);
        g_fs->png_dsc = NULL;
    }

    free(g_fs);
    g_fs = NULL;
    ESP_LOGI(TAG, "Fullscreen closed");
}

static void fullscreen_close_cb(lv_event_t *e)
{
    (void)e;
    /* Delete the fullscreen screen so LVGL falls back to the gallery grid.
     * The grid screen was loaded by GalleryApp::run(), kept alive as
     * lv_screen_load() only makes it inactive. Deleting the fullscreen
     * screen causes LVGL to re-activate the previously-loaded grid screen. */
    fullscreen_cleanup();
}

static void build_lvgl_fs_path(const char *sd_path, char *out, size_t out_len)
{
    const char *prefix = "/sdcard";
    size_t prefix_len = strlen(prefix);
    if (strncmp(sd_path, prefix, prefix_len) == 0) {
        snprintf(out, out_len, "%s%s", SD_MOUNT_PREFIX, sd_path + prefix_len);
    } else {
        snprintf(out, out_len, "%s/%s", SD_MOUNT_PREFIX, sd_path);
    }
}

/* ============ Fast PNG Loader (RGB565 direct, no ARGB8888 overhead) ============ */

/* Load a PNG via LVGL FS, decode directly to RGB565.
 * Skips the ARGB8888 intermediate step that the stock decoder uses.
 * Returns an lv_image_dsc_t for use with lv_image_set_src().
 * Caller must call free_png_rgb565() to release memory after image is no longer displayed.
 */
static lv_image_dsc_t *load_png_rgb565(const char *lvgl_path, int max_w, int max_h)
{
    /* 1. Read file through LVGL filesystem */
    lv_fs_file_t f;
    if (lv_fs_open(&f, lvgl_path, LV_FS_MODE_RD) != LV_FS_RES_OK) {
        ESP_LOGE(TAG, "Cannot open: %s", lvgl_path);
        return NULL;
    }
    uint32_t file_size;
    lv_fs_seek(&f, 0, LV_FS_SEEK_END);
    lv_fs_tell(&f, &file_size);
    lv_fs_seek(&f, 0, LV_FS_SEEK_SET);

    uint8_t *src = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!src) src = malloc(file_size);
    if (!src) { lv_fs_close(&f); return NULL; }

    uint32_t rn;
    lv_fs_read(&f, src, file_size, &rn);
    lv_fs_close(&f);
    if (rn != file_size) { free(src); return NULL; }

    /* 2. Decode PNG → RGB888 (PNG_FORMAT_BGR = 3 bytes/px, no alpha waste) */
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, src, file_size)) {
        ESP_LOGE(TAG, "PNG header: %s", img.message);
        free(src);
        return NULL;
    }
    img.format = PNG_FORMAT_BGR;

    int src_w = (int)img.width;
    int src_h = (int)img.height;

    /* Calculate display size (aspect-ratio preserving, clamped to max) */
    int disp_w = src_w, disp_h = src_h;
    if (disp_w > max_w || disp_h > max_h) {
        float s = (float)max_w / (disp_w > disp_h ? disp_w : disp_h);
        disp_w = (int)(disp_w * s);
        disp_h = (int)(disp_h * s);
    }
    if (disp_w < 1) disp_w = 1;
    if (disp_h < 1) disp_h = 1;

    size_t rgb888_size = (size_t)src_w * src_h * 3;
    uint8_t *rgb888 = heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb888) rgb888 = malloc(rgb888_size);
    if (!rgb888) { png_image_free(&img); free(src); return NULL; }

    if (!png_image_finish_read(&img, NULL, rgb888, 0, NULL)) {
        ESP_LOGE(TAG, "PNG decode: %s", img.message);
        png_image_free(&img); free(rgb888); free(src);
        return NULL;
    }
    png_image_free(&img);
    free(src);

    /* 3. Convert RGB888 → RGB565, with optional nearest-neighbor downscale */
    size_t out_size = (size_t)disp_w * disp_h * sizeof(uint16_t);
    uint16_t *rgb565 = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb565) rgb565 = malloc(out_size);
    if (!rgb565) { free(rgb888); return NULL; }

    if (src_w == disp_w && src_h == disp_h) {
        /* 1:1 — fast path */
        for (int i = 0; i < src_w * src_h; i++) {
            uint8_t b = rgb888[i * 3 + 0];  /* PNG_FORMAT_BGR → B,G,R */
            uint8_t g = rgb888[i * 3 + 1];
            uint8_t r = rgb888[i * 3 + 2];
            rgb565[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    } else {
        /* Downscale: bilinear interpolation (same quality as LVGL renderer) */
        int sy_acc = src_h / 2;  /* half-pixel offset for correct sampling */
        for (int dy = 0; dy < disp_h; dy++) {
            sy_acc += src_h;
            int sy0 = sy_acc / disp_h;
            if (sy0 >= src_h) sy0 = src_h - 1;
            int sy1 = sy0 + 1;
            if (sy1 >= src_h) sy1 = sy0;
            int fy = ((sy_acc % disp_h) * 256) / disp_h;

            int sx_acc = src_w / 2;
            uint16_t *row = &rgb565[dy * disp_w];
            for (int dx = 0; dx < disp_w; dx++) {
                sx_acc += src_w;
                int sx0 = sx_acc / disp_w;
                if (sx0 >= src_w) sx0 = src_w - 1;
                int sx1 = sx0 + 1;
                if (sx1 >= src_w) sx1 = sx0;
                int fx = ((sx_acc % disp_w) * 256) / disp_w;

                int w00 = (256 - fx) * (256 - fy);
                int w01 = fx * (256 - fy);
                int w10 = (256 - fx) * fy;
                int w11 = fx * fy;

                int s00 = sy0 * src_w + sx0;
                int s01 = sy0 * src_w + sx1;
                int s10 = sy1 * src_w + sx0;
                int s11 = sy1 * src_w + sx1;

                int r = (rgb888[s00*3+2]*w00 + rgb888[s01*3+2]*w01 +
                         rgb888[s10*3+2]*w10 + rgb888[s11*3+2]*w11) >> 16;
                int g = (rgb888[s00*3+1]*w00 + rgb888[s01*3+1]*w01 +
                         rgb888[s10*3+1]*w10 + rgb888[s11*3+1]*w11) >> 16;
                int b = (rgb888[s00*3+0]*w00 + rgb888[s01*3+0]*w01 +
                         rgb888[s10*3+0]*w10 + rgb888[s11*3+0]*w11) >> 16;

                row[dx] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }
    }
    free(rgb888);

    /* 4. Wrap in lv_image_dsc_t */
    lv_image_dsc_t *dsc = calloc(1, sizeof(lv_image_dsc_t));
    if (!dsc) { free(rgb565); return NULL; }
    dsc->header.w   = (uint32_t)disp_w;
    dsc->header.h   = (uint32_t)disp_h;
    dsc->header.cf  = LV_COLOR_FORMAT_RGB565;
    dsc->header.stride = (uint32_t)(disp_w * sizeof(uint16_t));
    dsc->data       = (const uint8_t *)rgb565;
    dsc->data_size  = (uint32_t)out_size;

    ESP_LOGI(TAG, "FastPNG: %dx%d → %dx%d RGB565 (%u B)", src_w, src_h, disp_w, disp_h, (unsigned)out_size);
    return dsc;
}

static void free_png_rgb565(lv_image_dsc_t *dsc)
{
    if (dsc) {
        if (dsc->data) free((void *)dsc->data);
        free(dsc);
    }
}

/* Open fullscreen for any file type */
static void gallery_open_fullscreen(const gallery_file_entry_t *entry)
{
    ESP_LOGI(TAG, "Opening: %s (type=%d)", entry->path, entry->type);

    fullscreen_ctx_t *ctx = (fullscreen_ctx_t *)calloc(1, sizeof(fullscreen_ctx_t));
    if (!ctx) return;
    g_fs = ctx;
    ctx->file_type = entry->type;

    /* Save the current active screen so we can restore it on close */
    ctx->prev_screen = lv_screen_active();

    /* Create fullscreen screen */
    ctx->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ctx->screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->screen, LV_OPA_COVER, 0);

    /*
     * 360x360 round screen: floating semi-transparent Back button
     * at bottom center. Round mask clips corners, so we keep the
     * button near the center of the safe zone (y ~ 155-170).
     */
    lv_obj_t *btn_back = lv_button_create(ctx->screen);
    lv_obj_set_size(btn_back, 120, 44);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(50, 50, 50), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_70, 0);
    lv_obj_set_style_radius(btn_back, 22, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_move_foreground(btn_back);

    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(label_back);
    lv_obj_set_style_text_color(label_back, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(btn_back, fullscreen_close_cb, LV_EVENT_CLICKED, NULL);

    /* File name label at top (safe zone) */
    ctx->label_name = lv_label_create(ctx->screen);
    lv_label_set_text(ctx->label_name, entry->name);
    lv_obj_set_style_text_color(ctx->label_name, lv_color_white(), 0);
    lv_obj_set_style_text_font(ctx->label_name, &lv_font_montserrat_14, 0);
    lv_obj_align(ctx->label_name, LV_ALIGN_TOP_MID, 0, 8);

    /* Build LVGL FS path */
    char lvgl_path[256];
    build_lvgl_fs_path(entry->path, lvgl_path, sizeof(lvgl_path));

    if (entry->type == GALLERY_FILE_AAF) {
        /* Read AAF file first, parse header to get actual video dimensions.
         * Handles _S (direct) and _R (redirect) formats. */
        FILE *fp = fopen(entry->path, "rb");
        if (!fp) {
            ESP_LOGE(TAG, "Cannot open AAF: %s", entry->path);
            fullscreen_cleanup();
            return;
        }
        fseek(fp, 0, SEEK_END);
        size_t file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (file_size == 0) { fclose(fp); fullscreen_cleanup(); return; }

        uint8_t *file_data = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!file_data) file_data = (uint8_t *)malloc(file_size);
        if (!file_data) { fclose(fp); fullscreen_cleanup(); return; }

        fread(file_data, 1, file_size, fp);
        fclose(fp);

        /* Parse AAF header to get video dimensions */
        int video_w = 260, video_h = 260;  /* default fallback */
        if (file_size >= 18 && memcmp(file_data, "_S", 2) == 0) {
            video_w = (int)(*(uint16_t *)(file_data + 10));
            video_h = (int)(*(uint16_t *)(file_data + 12));
        } else if (file_size >= 4 && memcmp(file_data, "_R", 2) == 0) {
            /* _R redirect: follow to referenced file, parse its _S header */
            uint8_t name_len = file_data[2];
            if (name_len > 0 && file_size >= (size_t)(3 + name_len)) {
                char ref_name[128];
                memcpy(ref_name, file_data + 3, name_len);
                ref_name[name_len] = '\0';
                /* Build path: same directory as the .aaf file */
                char ref_path[384];
                const char *last_slash = strrchr(entry->path, '/');
                if (last_slash) {
                    size_t dir_len = (size_t)(last_slash - entry->path);
                    snprintf(ref_path, sizeof(ref_path), "%.*s/%s", (int)dir_len, entry->path, ref_name);
                } else {
                    snprintf(ref_path, sizeof(ref_path), "/sdcard/%s", ref_name);
                }
                ESP_LOGI(TAG, "AAF redirect: %s -> %s", ref_name, ref_path);
                FILE *rfp = fopen(ref_path, "rb");
                if (rfp) {
                    uint8_t hdr[18];
                    if (fread(hdr, 1, 18, rfp) == 18 && memcmp(hdr, "_S", 2) == 0) {
                        video_w = (int)(*(uint16_t *)(hdr + 10));
                        video_h = (int)(*(uint16_t *)(hdr + 12));
                    }
                    fclose(rfp);
                }
            }
        }
        /* Clamp to round 360x360 safe area */
        #define MAX_DIM 260
        if (video_w > MAX_DIM) video_w = MAX_DIM;
        if (video_h > MAX_DIM) video_h = MAX_DIM;
        if (video_w < 16) video_w = 16;
        if (video_h < 16) video_h = 16;
        #undef MAX_DIM

        ctx->canvas_w = video_w;
        ctx->canvas_h = video_h;
        /* Set aaf_video_w/h to canvas size so flush callback uses zero offset */
        ctx->aaf_video_w = video_w;
        ctx->aaf_video_h = video_h;

        ESP_LOGI(TAG, "AAF video: %dx%d, canvas: %dx%d",
                 video_w, video_h, ctx->canvas_w, ctx->canvas_h);

        /* Create canvas sized exactly to video dimensions, centered on screen */
        ctx->canvas = lv_canvas_create(ctx->screen);
        lv_obj_set_size(ctx->canvas, ctx->canvas_w, ctx->canvas_h);
        lv_obj_align(ctx->canvas, LV_ALIGN_CENTER, 0, -4);
        lv_obj_clear_flag(ctx->canvas, LV_OBJ_FLAG_SCROLLABLE);

        uint8_t *canvas_buf_raw = (uint8_t *)heap_caps_malloc(
            ctx->canvas_w * ctx->canvas_h * sizeof(uint16_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!canvas_buf_raw) {
            canvas_buf_raw = (uint8_t *)malloc(ctx->canvas_w * ctx->canvas_h * sizeof(uint16_t));
        }
        if (canvas_buf_raw) {
            memset(canvas_buf_raw, 0, ctx->canvas_w * ctx->canvas_h * sizeof(uint16_t));
            lv_draw_buf_t *draw_buf = (lv_draw_buf_t *)calloc(1, sizeof(lv_draw_buf_t));
            if (draw_buf) {
                uint32_t buf_size = ctx->canvas_w * ctx->canvas_h * sizeof(uint16_t);
                lv_draw_buf_init(draw_buf,
                                 (uint32_t)ctx->canvas_w,
                                 (uint32_t)ctx->canvas_h,
                                 LV_COLOR_FORMAT_RGB565,
                                 ctx->canvas_w * sizeof(uint16_t),
                                 canvas_buf_raw,
                                 buf_size);
                lv_canvas_set_draw_buf(ctx->canvas, draw_buf);
            }
        }

        lv_screen_load(ctx->screen);

        anim_player_config_t player_cfg = {
            .flush_cb = anim_flush_cb,
            .update_cb = anim_update_cb,
            .user_data = ctx,
            .flags = {.swap = true},
            .task = ANIM_PLAYER_INIT_CONFIG()
        };
        player_cfg.task.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        player_cfg.task.task_affinity = 1;

        ctx->player_handle = anim_player_init(&player_cfg);
        if (!ctx->player_handle) {
            free(file_data);
            fullscreen_cleanup();
            return;
        }

        esp_err_t ret = anim_player_set_src_data(ctx->player_handle, file_data, file_size);
        if (ret != ESP_OK) {
            free(file_data);
            fullscreen_cleanup();
            return;
        }

        uint32_t start, end;
        anim_player_get_segment(ctx->player_handle, &start, &end);
        anim_player_set_segment(ctx->player_handle, start, end, 25, true);
        anim_player_update(ctx->player_handle, PLAYER_ACTION_START);
        ctx->player_running = true;

        /* LVGL timer to refresh canvas */
        ctx->refresh_timer = lv_timer_create(canvas_refresh_timer_cb, 33, ctx);

    } else {
        /* PNG / JPG: show spinner, defer heavy decode so UI doesn't freeze */
        /* Read header first to know native dimensions */
        lv_image_header_t header;
        ctx->img_disp_w = 0;
        ctx->img_disp_h = 0;
        if (lv_image_decoder_get_info(lvgl_path, &header) == LV_RESULT_OK) {
            int w = (int)header.w;
            int h = (int)header.h;
            #define MAX_DIM 260
            if (w > MAX_DIM || h > MAX_DIM) {
                float scale = (float)MAX_DIM / (w > h ? w : h);
                w = (int)(w * scale);
                h = (int)(h * scale);
            }
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            #undef MAX_DIM
            ctx->img_disp_w = w;
            ctx->img_disp_h = h;
            ESP_LOGI(TAG, "Image native %dx%d, display %dx%d", (int)header.w, (int)header.h, w, h);
        }
        snprintf(ctx->lvgl_path, sizeof(ctx->lvgl_path), "%s", lvgl_path);

        /* Show loading spinner immediately */
        ctx->loading_label = lv_label_create(ctx->screen);
        lv_label_set_text(ctx->loading_label, LV_SYMBOL_REFRESH " Loading...");
        lv_obj_set_style_text_color(ctx->loading_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(ctx->loading_label, &lv_font_montserrat_18, 0);
        lv_obj_center(ctx->loading_label);

        lv_screen_load(ctx->screen);

        /* Defer actual decode to next LVGL tick — user sees spinner instantly */
        ctx->defer_timer = lv_timer_create(defer_image_decode_cb, 50, ctx);
        lv_timer_set_repeat_count(ctx->defer_timer, 1);
    }
}

/* ============ Thumbnail Grid ============ */

static uint8_t str_hash(const char *name)
{
    uint8_t h = 0;
    while (*name) h = (h * 31) + (uint8_t)(*name++);
    return h;
}

static void thumbnail_click_cb(lv_event_t *e)
{
    gallery_file_entry_t *entry = (gallery_file_entry_t *)lv_event_get_user_data(e);
    if (!entry) {
        ESP_LOGW(TAG, "Thumbnail clicked but no entry data");
        return;
    }
    ESP_LOGI(TAG, "Thumbnail clicked: %s", entry->name);
    gallery_open_fullscreen(entry);
}

static lv_obj_t *create_thumbnail(lv_obj_t *parent, const gallery_file_entry_t *entry)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, THUMB_W, THUMB_H);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Placeholder background */
    lv_obj_t *placeholder = lv_obj_create(cont);
    lv_obj_set_size(placeholder, THUMB_W - 8, THUMB_IMG_H);
    lv_obj_align(placeholder, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(placeholder, 8, 0);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);

    if (entry->type == GALLERY_FILE_AAF) {
        uint8_t hash = str_hash(entry->name);
        lv_obj_set_style_bg_color(placeholder, thumb_colors[hash % THUMB_COLORS_COUNT], 0);
        lv_obj_set_style_bg_opa(placeholder, LV_OPA_COVER, 0);

        lv_obj_t *play_icon = lv_label_create(placeholder);
        lv_label_set_text(play_icon, LV_SYMBOL_PLAY);
        lv_obj_center(play_icon);
        lv_obj_set_style_text_color(play_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(play_icon, &lv_font_montserrat_24, 0);
    } else {
        /* PNG/JPG: colored placeholder with image icon (avoids decoding full image for thumbnail) */
        uint8_t hash = str_hash(entry->name);
        lv_color_t bg = thumb_colors[(hash + 3) % THUMB_COLORS_COUNT];
        lv_obj_set_style_bg_color(placeholder, bg, 0);
        lv_obj_set_style_bg_opa(placeholder, LV_OPA_COVER, 0);

        lv_obj_t *img_icon = lv_label_create(placeholder);
        lv_label_set_text(img_icon, LV_SYMBOL_IMAGE);
        lv_obj_center(img_icon);
        lv_obj_set_style_text_color(img_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(img_icon, &lv_font_montserrat_24, 0);
    }

    /* File name label */
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_set_width(label, THUMB_W - 8);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, entry->name);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cont, thumbnail_click_cb, LV_EVENT_CLICKED, (void *)entry);

    return cont;
}

void gallery_ui_init(lv_obj_t *parent, gallery_file_list_t *file_list)
{
    /* Register SD card LVGL filesystem driver */
    sd_fs_register();

    if (!file_list || file_list->count == 0) {
        ESP_LOGW(TAG, "No images/animations found");

        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label,
            "No images found\n\n"
            "Place .aaf, .png, or .jpg\n"
            "files in /sdcard/");
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_make(150, 150, 150), 0);
        return;
    }

    ESP_LOGI(TAG, "Creating gallery grid: %d files", file_list->count);

    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_set_size(scroll, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(scroll, GRID_PAD_X, 0);
    lv_obj_set_style_pad_gap(scroll, THUMB_GAP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < file_list->count; i++) {
        create_thumbnail(scroll, &file_list->entries[i]);
    }
}
