"""Extract the approved Xiaolan animation rows into LVGL ARGB8888 frames."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root.parent / "codex-pet-xiaolan-v2" / "final" / "spritesheet-extended.png"
out_dir = root / "firmware" / "main" / "app_xiaolan" / "assets"
out_dir.mkdir(parents=True, exist_ok=True)

atlas = Image.open(source).convert("RGBA")
frame_width, frame_height = 96, 104
rows = {
    "idle": (0, 7),
    "active": (4, 5),
    "working": (7, 6),
    "success": (3, 4),
    "failed": (5, 8),
    "rest": (6, 6),
}
frames = {}
for name, (row, count) in rows.items():
    frames[name] = []
    for index in range(count):
        cell = atlas.crop((index * 192, row * 208, (index + 1) * 192, (row + 1) * 208))
        cell = cell.resize((frame_width, frame_height), Image.Resampling.NEAREST)
        pixels = bytearray()
        for red, green, blue, alpha in cell.getdata():
            # LVGL's ARGB8888 pixels are stored in memory as BGRA on ESP32
            # (lv_color32_t fields are blue, green, red, alpha).
            pixels.extend((blue, green, red, alpha) if alpha else (0, 0, 0, 0))
        frames[name].append(bytes(pixels))

header = ["#pragma once", "#include \"lvgl.h\"", ""]
for name, data in frames.items():
    header.append(f"#define XIAOLAN_{name.upper()}_FRAMES {len(data)}")
header.extend(["", "#ifdef __cplusplus", "extern \"C\" {", "#endif"])
for name in frames:
    header.append(f"extern const lv_image_dsc_t xiaolan_{name}[XIAOLAN_{name.upper()}_FRAMES];")
header.extend(["#ifdef __cplusplus", "}", "#endif", ""])
source_lines = ["#include \"xiaolan_assets.hpp\"", ""]
for name, state_frames in frames.items():
    for index, data in enumerate(state_frames):
        source_lines.append(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t xiaolan_{name}_{index}_map[] = {{")
        for offset in range(0, len(data), 24):
            source_lines.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 24]) + ",")
        source_lines.extend(["};", ""])
    source_lines.append(f"const lv_image_dsc_t xiaolan_{name}[XIAOLAN_{name.upper()}_FRAMES] = {{")
    for index in range(len(state_frames)):
        source_lines.extend([
            "    {",
            "        .header.cf = LV_COLOR_FORMAT_ARGB8888,",
            "        .header.magic = LV_IMAGE_HEADER_MAGIC,",
            f"        .header.w = {frame_width},",
            f"        .header.h = {frame_height},",
            f"        .data_size = sizeof(xiaolan_{name}_{index}_map),",
            f"        .data = xiaolan_{name}_{index}_map,",
            "    },",
        ])
    source_lines.append("};")
    source_lines.append("")
if source_lines and source_lines[-1] == "":
    source_lines.pop()
(out_dir / "xiaolan_assets.hpp").write_text("\n".join(header) + "\n", encoding="utf-8")
(out_dir / "xiaolan_assets.c").write_text("\n".join(source_lines) + "\n", encoding="utf-8")
print(f"wrote {len(frames)} frames to {out_dir}")
