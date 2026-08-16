#pragma once

#include "esp_lcd_panel_io.h"

void LvglPort_Init();
void LvglPort_Loop();

// SPI DMA-done callback: calls lv_disp_flush_ready when the transfer finishes.
// Registered in QSPI_Init as io_config.on_color_trans_done.
bool lvglOnDmaDone(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* data, void* user_ctx);