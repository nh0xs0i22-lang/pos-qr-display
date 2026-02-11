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
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"

#include "app_config.h"
#include "lcd_st7701.h"
#include "touch_gt911.h"
#include "wifi_service.h"
#include "time_service.h"
#include "mqtt_service.h"
#include "ui.h"
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

    bool     showing_qr  = false;
    uint32_t last_qr_gen = 0;

    for (;;) {
        bool     has_qr = mqtt_service_has_qr_data();
        uint32_t qr_gen = mqtt_service_get_qr_gen();

        if (!has_qr) {
            /* MQTT says no QR data → ensure idle screen, reset dismiss */
            if (showing_qr) {
                qr_screen_hide();
                showing_qr = false;
            }
            qr_screen_clear_dismissed();
        } else {
            /* New payload arrived → reset dismiss so QR can show */
            if (qr_gen != last_qr_gen) {
                qr_screen_clear_dismissed();
                last_qr_gen = qr_gen;
            }

            if (!qr_screen_is_dismissed()) {
                qr_screen_show(mqtt_service_get_qr());
                showing_qr = true;
            } else {
                showing_qr = false;
            }
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

    /* 5. Initialise touch (GT911 over I2C) */
    ESP_ERROR_CHECK(touch_gt911_init());
    ESP_ERROR_CHECK(touch_gt911_register_lvgl());
    ESP_LOGI(TAG, "Touch initialised");

    /* 6. Create glassmorphism idle screen and make it active */
    ui_init(disp);

    /* 7. Create QR screen (captures the active screen as its idle target) */
    qr_screen_init(disp);

    /* 8. Initialise WiFi (NVS + STA, non-blocking) */
    wifi_service_init();

    /* 9. Start SNTP (retries in background until WiFi connects) */
    time_service_init();

    /* 10. Start MQTT service */
    ESP_ERROR_CHECK(mqtt_service_init());

    /* 11. Start LVGL handler task (includes MQTT→UI polling) */
    xTaskCreate(lvgl_task, "lvgl", APP_LVGL_TASK_STACK, NULL,
                APP_LVGL_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "System running");
}
