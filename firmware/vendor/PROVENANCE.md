# Vendored board drivers — provenance

These files are fetched verbatim from `streetlightstartupnotes/Codex-Macro32`
(firmware/Codex-Micro-1.0), which carries `LICENSE` = MIT © 2026 imliubo and
builds on Waveshare/Espressif sample drivers under Apache-2.0.

That repository's `LIMITED_SOURCE_USE.md` states plainly:

> 不能把本说明解释为撤销、替代或限制第三方 MIT、Apache-2.0 或其他许可证已经授予的权利。

so the driver layer below is used under its original MIT / Apache-2.0 terms with
copyright headers intact. Not taken from that project: the BLE transport, the
Codex product UI, and the macOS Python companion — the DSH build replaces all
three, and the author's original UI expression is outside the permissive layer.

| file | licence header | why it is here |
| --- | --- | --- |
| `esp_lcd_st77916.c` | Apache-2.0 | ST77916 panel driver (Apache-2.0, Espressif/Waveshare) |
| `esp_lcd_st77916.h` | Apache-2.0 | ST77916 panel driver header |
| `Display_ST77916.cpp` | see file head | board display bring-up: QSPI bus, backlight, rotation |
| `Display_ST77916.h` | see file head | display bring-up header |
| `Touch_CST816.cpp` | see file head | CST816 capacitive touch over I2C |
| `Touch_CST816.h` | see file head | touch header |
| `I2C_Driver.cpp` | see file head | shared I2C bus helper (GPIO10/11) |
| `I2C_Driver.h` | see file head | I2C helper header |
| `LvglPort.cpp` | see file head | LVGL flush/read glue for this panel |
| `LvglPort.h` | see file head | LVGL port header |
| `AudioFeedback.cpp` | see file head | **ES8311** I2S codec tone output. The 1.85B carries a PCM5101 DAC (no codec), so `AudioFeedback::begin()` is expected to fail here and the firmware runs silent — see `README`/部署指南. |
| `AudioFeedback.h` | see file head | audio header |
| `lv_conf.h` | see file head | LVGL configuration tuned for this board. **Two lines changed from the Codex upstream**: `LV_FONT_MONTSERRAT_48` set to 1 (phase glyph) and `LV_USE_QRCODE` set to 1 (provisioning portal). |
| `platformio.ini.reference` | see file head | verified pioarduino platform + board fuses |
| `es8311.cpp` | Apache-2.0 | ES8311 I2S audio codec driver (I2C) |
| `es8311.h` | Apache-2.0 | ES8311 header |
| `es8311_reg.h` | Apache-2.0 | ES8311 register map |

## Upstream chain

`imliubo/codex-micro-4-core2` (MIT) → `Codex-Macro32` (port to 1.85B) → this project (DSH retarget).

Fetched 2026-08-16T00:49:33.740Z.
