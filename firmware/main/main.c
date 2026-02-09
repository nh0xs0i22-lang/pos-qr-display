/*
 * Display test – LVGL + ST7701S
 *
 * Minimal main that initialises LVGL and the LCD driver, then draws
 * a static test pattern to verify display quality and colour mapping.
 * No WiFi, MQTT, touch, or UI logic.
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

static const char *TAG = "main";

/* ── LVGL tick source (1 ms periodic timer) ───────────────────────────── */

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(APP_LVGL_TICK_MS);
}

/* ── LVGL handler task ────────────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task running");
    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ── Test screen ──────────────────────────────────────────────────────── *
 *                                                                         *
 *   ┌──────────────────────────────────┐                                  *
 *   │ RED              GREEN           │                                  *
 *   │                                  │                                  *
 *   │          ┌──────────┐            │                                  *
 *   │          │  WHITE   │            │  dark-gray background            *
 *   │          └──────────┘            │                                  *
 *   │                                  │                                  *
 *   │ BLUE             WHITE           │                                  *
 *   └──────────────────────────────────┘                                  *
 *                                                                         *
 *  Corner patches verify RGB channel mapping is correct.                  *
 * ─────────────────────────────────────────────────────────────────────── */

static void test_screen_create(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    /* Dark-gray background */
    lv_obj_set_style_bg_color(scr, lv_color_make(0x40, 0x40, 0x40), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Centered white rectangle (200×200) */
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 200, 200);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    /* Corner colour patches – verify R/G/B channel mapping */
    static const struct {
        lv_align_t align;
        lv_coord_t x, y;
        uint8_t r, g, b;
    } corners[] = {
        { LV_ALIGN_TOP_LEFT,      20,  20,  0xFF, 0x00, 0x00 }, /* Red   */
        { LV_ALIGN_TOP_RIGHT,    -20,  20,  0x00, 0xFF, 0x00 }, /* Green */
        { LV_ALIGN_BOTTOM_LEFT,   20, -20,  0x00, 0x00, 0xFF }, /* Blue  */
        { LV_ALIGN_BOTTOM_RIGHT, -20, -20,  0xFF, 0xFF, 0xFF }, /* White */
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *p = lv_obj_create(scr);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, 60, 60);
        lv_obj_align(p, corners[i].align, corners[i].x, corners[i].y);
        lv_obj_set_style_bg_color(p,
            lv_color_make(corners[i].r, corners[i].g, corners[i].b), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    }
}

/* ── Entry point ──────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Display Test ===");

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

    /* 5. Draw static test pattern */
    test_screen_create(disp);
    ESP_LOGI(TAG, "Test screen created");

    /* 6. Start LVGL handler task */
    xTaskCreate(lvgl_task, "lvgl", APP_LVGL_TASK_STACK, NULL,
                APP_LVGL_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "Display test running");
}
