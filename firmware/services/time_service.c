/*
 * SNTP time service – obtains wall-clock time from an NTP pool.
 *
 * Initialisation is non-blocking: the lwIP SNTP client sends periodic
 * requests in the background.  When WiFi comes up and a response
 * arrives, the system clock is stepped and the sync callback sets the
 * flag returned by time_service_is_time_valid().
 *
 * The timezone is applied before the first sync so that localtime_r()
 * returns local time as soon as the clock is set.
 */

#include "time_service.h"

#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "time_svc";

/* POSIX TZ string – adjust for your locale.
   ICT = Indochina Time, UTC+7, no DST. */
#define APP_TZ          "ICT-7"

#define APP_NTP_SERVER  "pool.ntp.org"

static volatile bool s_valid;

/* ── Sync callback (called in lwIP / tcpip task context) ─────────────── */

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    s_valid = true;

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG, "Synchronised: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void time_service_init(void)
{
    /* Set timezone before the first sync so localtime_r() returns
       local time immediately once the clock is set. */
    setenv("TZ", APP_TZ, 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_NTP_SERVER);
    cfg.sync_cb = on_time_sync;

    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));

    ESP_LOGI(TAG, "SNTP started – server %s, TZ %s",
             APP_NTP_SERVER, APP_TZ);
}

bool time_service_is_time_valid(void)
{
    return s_valid;
}
