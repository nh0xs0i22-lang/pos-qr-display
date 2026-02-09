#pragma once

#include <stdbool.h>

/**
 * Start the SNTP client and set the system timezone.
 *
 * Safe to call before WiFi is connected – the underlying lwIP SNTP
 * client retries periodically until a server responds.
 * Non-blocking; returns immediately.
 */
void time_service_init(void);

/**
 * Returns true once SNTP has synchronised the system clock.
 */
bool time_service_is_time_valid(void);
