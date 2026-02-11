#pragma once

#include "lvgl.h"

/**
 * Create the programmatic rain background scene.
 * Call once during UI init — all objects are parented to @p parent.
 */
void bg_rain_create(lv_obj_t *parent);
