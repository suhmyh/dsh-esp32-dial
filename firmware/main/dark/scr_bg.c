#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_SCR_BG
#define LV_ATTRIBUTE_IMAGE_SCR_BG
#endif

// Neutral charcoal (#12151C), RGB565.  The range initializer keeps this
// generated asset compact while still providing the full 360x360 image.
const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_SCR_BG
uint16_t scr_bg_map[360 * 360] = {
  [0 ... (360 * 360 - 1)] = 0x10A3,
};

const lv_image_dsc_t scr_bg = {
  .header.cf = LV_COLOR_FORMAT_RGB565,
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.w = 360,
  .header.h = 360,
  .data_size = 360 * 360 * 2,
  .data = (const uint8_t *)scr_bg_map,
};
