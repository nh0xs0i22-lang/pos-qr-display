#pragma once

#include "lvgl.h"
#include "mqtt_service.h"

/**
 * Create QR and idle screens.  Call once after LVGL display is registered.
 */
void qr_screen_init(lv_disp_t *disp);

/**
 * Update the QR code data / labels and switch to the QR screen.
 */
void qr_screen_show(const qr_payload_t *payload);

/**
 * Show a static (non-MQTT) QR code.  Reuses the same QR screen/widget.
 * Tapping the screen while a static QR is visible returns to idle
 * without setting the MQTT dismiss flag.
 */
void qr_screen_show_static(const char *qr_data, const char *amount,
                            const char *desc);

/**
 * Switch back to the idle (blank) screen.
 */
void qr_screen_hide(void);

/**
 * Returns true if the user explicitly dismissed the QR screen via touch.
 */
bool qr_screen_is_dismissed(void);

/**
 * Reset the dismiss flag (e.g. when new MQTT QR data arrives).
 */
void qr_screen_clear_dismissed(void);
