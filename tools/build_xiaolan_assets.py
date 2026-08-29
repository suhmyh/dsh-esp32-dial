"""Extract the approved Xiaolan idle row into compact LVGL ARGB8888 frames."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root.parent / "codex-pet-xiaolan-v2" / "final" / "spritesheet-extended.png"
out_dir = root / "firmware" / "main" / "app_xiaolan" / "assets"
out_dir.mkdir(parents=True, exist_ok=True)

atlas = Image.open(source).convert("RGBA")
frames = []
for index in range(7):
    cell = atlas.crop((index * 192, 0, (index + 1) * 192, 208))
    cell = cell.resize((128, 139), Image.Resampling.NEAREST)
    pixels = bytearray()
    for red, green, blue, alpha in cell.getdata():
        # LVGL's ARGB8888 pixels are stored in memory as BGRA on ESP32
        # (lv_color32_t fields are blue, green, red, alpha).
        pixels.extend((blue, green, red, alpha) if alpha else (0, 0, 0, 0))
    frames.append(bytes(pixels))

header = ["#pragma once", "#include \"lvgl.h\"", "", "#define XIAOLAN_IDLE_FRAMES 7", "#ifdef __cplusplus", "extern \"C\" {", "#endif", "extern const lv_image_dsc_t xiaolan_idle[XIAOLAN_IDLE_FRAMES];", "#ifdef __cplusplus", "}", "#endif", ""]
source_lines = ["#include \"xiaolan_assets.hpp\"", ""]
for index, data in enumerate(frames):
    source_lines.append(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t xiaolan_idle_{index}_map[] = {{")
    for offset in range(0, len(data), 24):
        source_lines.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 24]) + ",")
    source_lines.extend(["};", ""])
source_lines.append("const lv_image_dsc_t xiaolan_idle[XIAOLAN_IDLE_FRAMES] = {")
for index in range(7):
    source_lines.extend([
        "    {",
        "        .header.cf = LV_COLOR_FORMAT_ARGB8888,",
        "        .header.magic = LV_IMAGE_HEADER_MAGIC,",
        "        .header.w = 128,",
        "        .header.h = 139,",
        f"        .data_size = sizeof(xiaolan_idle_{index}_map),",
        f"        .data = xiaolan_idle_{index}_map,",
        "    },",
    ])
source_lines.append("};")
(out_dir / "xiaolan_assets.hpp").write_text("\n".join(header) + "\n", encoding="utf-8")
(out_dir / "xiaolan_assets.c").write_text("\n".join(source_lines) + "\n", encoding="utf-8")
print(f"wrote {len(frames)} frames to {out_dir}")
