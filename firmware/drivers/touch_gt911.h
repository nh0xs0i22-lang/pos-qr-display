#pragma once

#include "esp_err.h"
#include "lvgl.h"

/**
 * Initialise GT911 touch controller over I2C.
 * Must be called after gpio/I2C bus is available.
 */
esp_err_t touch_gt911_init(void);

/**
 * Register the touch controller as an LVGL pointer input device.
 * Must be called after lv_init() and after touch_gt911_init().
 */
esp_err_t touch_gt911_register_lvgl(void);
