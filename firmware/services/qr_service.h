#pragma once

#include "esp_err.h"

/**
 * Initialise the QR service.
 *
 * Pre-allocates any buffers needed for QR code generation so that the
 * render path is free of dynamic allocation.
 */
esp_err_t qr_service_init(void);
