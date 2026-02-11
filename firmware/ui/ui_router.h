#pragma once

#include "esp_err.h"
#include "lvgl.h"

typedef enum {
    UI_STATE_IDLE,
    UI_STATE_QR_DISPLAY,
    UI_STATE_RESULT,
} ui_state_t;

/**
 * Create all LVGL screens and enter the IDLE (screensaver) state.
 */
esp_err_t ui_router_init(lv_disp_t *disp);

/**
 * Transition to a new UI state.
 */
esp_err_t ui_router_set_state(ui_state_t state);

/**
 * Return the current UI state.
 */
ui_state_t ui_router_get_state(void);
