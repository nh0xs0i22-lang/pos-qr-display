#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/**
 * Initialise the ST7701 RGB LCD panel.
 *
 * Sends the ST7701S register init sequence over 3-wire SPI, then
 * creates an ESP-IDF RGB panel with double PSRAM-backed framebuffers.
 */
esp_err_t lcd_st7701_init(esp_lcd_panel_handle_t *out_panel);

/**
 * Register the panel with LVGL.
 *
 * Uses the panel's PSRAM framebuffers directly (zero-copy, direct mode)
 * and synchronises flushes to VSYNC for tear-free output.
 */
esp_err_t lcd_st7701_register_lvgl(esp_lcd_panel_handle_t panel,
                                   lv_disp_t **out_disp);
