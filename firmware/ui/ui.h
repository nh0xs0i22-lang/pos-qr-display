#pragma once

#include "lvgl.h"

/**
 * Create and show the glassmorphism idle screen (clock + VietQR tap).
 * Call once after lv_init() and display registration, before qr_screen_init().
 */
void ui_init(lv_disp_t *disp);
