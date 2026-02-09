/*
 * WiFi service – STA mode with automatic reconnect.
 *
 * Initialises NVS, esp_netif, the default event loop, and the WiFi
 * driver.  Connection is event-driven; wifi_service_init() returns
 * immediately and the STA_START event triggers the first connect.
 *
 * On disconnect the handler retries up to APP_WIFI_MAX_RETRY times.
 * If the retry limit is hit the service stops reconnecting and logs
 * an error.  A successful connection (GOT_IP) always resets the
 * counter, so a later disconnect restarts the full retry budget.
 */

#include "wifi_service.h"
#include "app_config.h"
#include "secrets/secrets.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

static volatile bool s_connected;
static int           s_retry_count;

/* ── Event handler ───────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {

        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting …");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            s_connected = false;
            if (s_retry_count < APP_WIFI_MAX_RETRY) {
                s_retry_count++;
                ESP_LOGW(TAG, "Disconnected – retry %d/%d",
                         s_retry_count, APP_WIFI_MAX_RETRY);
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "Retry limit reached (%d)",
                         APP_WIFI_MAX_RETRY);
            }
            break;
        }

        default:
            break;
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "Connected – IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        s_connected   = true;
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void wifi_service_init(void)
{
    /* ── NVS (required by the WiFi driver) ───────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Network interface + default event loop ──────────────────── */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* ── WiFi driver ─────────────────────────────────────────────── */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    /* ── Event handlers ──────────────────────────────────────────── */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, NULL, NULL));

    /* ── STA configuration ───────────────────────────────────────── */
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    /* strncpy is safe – wifi_cfg is zero-initialised above. */
    strncpy((char *)wifi_cfg.sta.ssid,
            APP_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password,
            APP_WIFI_PASS, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    /* ── Start (STA_START event triggers esp_wifi_connect) ───────── */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Initialised – SSID \"%s\", max retries %d",
             APP_WIFI_SSID, APP_WIFI_MAX_RETRY);
}

bool wifi_service_is_connected(void)
{
    return s_connected;
}
