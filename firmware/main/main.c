/*
 * POS QR Display – main
 *
 * Initialises LCD + LVGL, starts MQTT, and polls for QR data.
 * WiFi must be initialised before mqtt_service_init() – add when ready.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#include "app_config.h"
#include "lcd_st7701.h"
#include "wifi_service.h"
#include "time_service.h"
#include "mqtt_service.h"
#include "idle_screen.h"
#include "qr_screen.h"

static const char *TAG = "main";

/* ── LVGL tick source (1 ms periodic timer) ───────────────────────────── */

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(APP_LVGL_TICK_MS);
}

/* ── LVGL handler task + MQTT→UI polling ──────────────────────────────── */

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task running");

    bool showing_qr = false;

    for (;;) {
        /* Poll MQTT state and drive screen transitions */
        bool has_qr = mqtt_service_has_qr_data();

        if (has_qr) {
            qr_screen_show(mqtt_service_get_qr());
            showing_qr = true;
        } else if (showing_qr) {
            qr_screen_hide();
            showing_qr = false;
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ── Entry point ──────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== POS QR Display ===");

    /* 1. Initialise LVGL library */
    lv_init();

    /* 2. Start 1 ms tick timer for LVGL */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer,
                                             APP_LVGL_TICK_MS * 1000));

    /* 3. Initialise LCD hardware (ST7701S + RGB panel) */
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(lcd_st7701_init(&panel));
    ESP_LOGI(TAG, "LCD panel initialised");

    /* 4. Register panel with LVGL (direct mode, PSRAM double buffer) */
    lv_disp_t *disp = NULL;
    ESP_ERROR_CHECK(lcd_st7701_register_lvgl(panel, &disp));
    ESP_LOGI(TAG, "LVGL display registered");

    /* 5. Create idle screen (flip clock) and make it active */
    idle_screen_init(disp);
    idle_screen_show();

    /* 6. Create QR screen (captures the active screen as its idle target) */
    qr_screen_init(disp);

    /* 7. Initialise WiFi (NVS + STA, non-blocking) */
    wifi_service_init();

    /* 8. Start SNTP (retries in background until WiFi connects) */
    time_service_init();

    /* 9. Start MQTT service */
    ESP_ERROR_CHECK(mqtt_service_init());

    /* 10. Start LVGL handler task (includes MQTT→UI polling) */
    xTaskCreate(lvgl_task, "lvgl", APP_LVGL_TASK_STACK, NULL,
                APP_LVGL_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "System running");
}
