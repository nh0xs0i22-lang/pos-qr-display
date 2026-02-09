#pragma once

#include "lvgl.h"

/**
 * Create the idle screen (flip-clock style HH:MM).
 * Call once after LVGL display is registered.
 */
void idle_screen_init(lv_disp_t *disp);

/**
 * Switch to the idle screen.
 */
void idle_screen_show(void);

/**
 * Called when leaving the idle screen.
 * The timer keeps ticking so the clock stays current.
 */
void idle_screen_hide(void);
