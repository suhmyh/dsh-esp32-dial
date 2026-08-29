"""Generate the neutral desktop wallpaper used by the LVGL launcher.

The original wallpaper is a saturated multicolor image.  A uniform deep
charcoal keeps the launcher readable and prevents the Xiaolan pet's colors
from being visually contaminated by the background.
"""

from pathlib import Path


WIDTH = 360
HEIGHT = 360
# Neutral charcoal, deliberately not blue or purple: RGB #12151C.
RGB565 = 0x10A3
PIXELS = WIDTH * HEIGHT


def main() -> None:
    target = Path(__file__).resolve().parents[1] / "firmware" / "main" / "dark" / "scr_bg.c"
    target.write_text(
        """#ifdef __has_include
    #if __has_include(\"lvgl.h\")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include \"lvgl.h\"
#else
    #include \"lvgl/lvgl.h\"
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
const uint16_t scr_bg_map[WIDTH * HEIGHT] = {
  [0 ... (WIDTH * HEIGHT - 1)] = RGB565_VALUE,
};

const lv_image_dsc_t scr_bg = {
  .header.cf = LV_COLOR_FORMAT_RGB565,
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.w = WIDTH,
  .header.h = HEIGHT,
  .data_size = WIDTH * HEIGHT * 2,
  .data = (const uint8_t *)scr_bg_map,
};
""".replace("WIDTH", str(WIDTH))
        .replace("HEIGHT", str(HEIGHT))
        .replace("RGB565_VALUE", f"0x{RGB565:04X}"),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
