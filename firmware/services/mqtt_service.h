#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Maximum field lengths (including NUL terminator).
   Sized so the struct fits comfortably in internal SRAM. */
#define QR_DATA_MAX     512
#define QR_AMOUNT_MAX   32
#define QR_DESC_MAX     64

/**
 * Payload received on the pos/qr/show topic.
 *
 * Expected JSON:
 *   { "qr_data": "<qr-string>", "amount": "150.00", "desc": "Order #1" }
 *
 * Only "qr_data" is mandatory; "amount" and "desc" default to empty.
 */
typedef struct {
    char data[QR_DATA_MAX];
    char amount[QR_AMOUNT_MAX];
    char desc[QR_DESC_MAX];
} qr_payload_t;

/**
 * Start the MQTT client.
 *
 * Connects to the broker (credentials from secrets/secrets.h) and
 * subscribes to pos/qr/show, pos/qr/hide, and pos/qr/result.
 * Requires WiFi to be connected first.
 */
esp_err_t mqtt_service_init(void);

/**
 * Returns true after a pos/qr/show message and false after pos/qr/hide.
 */
bool mqtt_service_has_qr_data(void);

/**
 * Return a pointer to the last received QR payload.
 *
 * Valid only when mqtt_service_has_qr_data() == true.
 * The pointer is to static internal storage; read it promptly and do not
 * cache the pointer across frames.
 */
const qr_payload_t *mqtt_service_get_qr(void);

/**
 * Generation counter – incremented each time a new pos/qr/show arrives.
 * Used by the UI loop to detect new payloads vs. the same old data.
 */
uint32_t mqtt_service_get_qr_gen(void);
