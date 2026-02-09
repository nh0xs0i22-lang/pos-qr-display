#pragma once

/* ── WiFi ─────────────────────────────────── */
#define APP_WIFI_SSID           "your-ssid"
#define APP_WIFI_PASS           "your-password"
#define APP_WIFI_MAX_RETRY      10

/* ── MQTT ─────────────────────────────────── */
#define APP_MQTT_BROKER_URI     "mqtt://your-broker:1883"
#define APP_MQTT_TOPIC_QR       "pos/qr/display"
#define APP_MQTT_TOPIC_RESULT   "pos/qr/result"

/* ── LCD (ST7701 RGB 480×480 RGB565) ──────── */
#define APP_LCD_H_RES           480
#define APP_LCD_V_RES           480

/* ── LVGL task ────────────────────────────── */
#define APP_LVGL_TICK_MS        1
#define APP_LVGL_TASK_STACK     (6 * 1024)
#define APP_LVGL_TASK_PRIO      2
