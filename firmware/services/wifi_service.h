#pragma once

#include <stdbool.h>

/**
 * Initialise WiFi in STA mode and begin connecting.
 *
 * Handles NVS, esp_netif, and event loop creation internally.
 * Credentials come from secrets/secrets.h, retry limit from app_config.h.
 * Returns immediately – connection proceeds in the background.
 */
void wifi_service_init(void);

/**
 * Returns true once an IP address has been obtained.
 */
bool wifi_service_is_connected(void);
