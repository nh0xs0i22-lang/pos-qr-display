#pragma once

/* ── WiFi ─────────────────────────────────── */
/* SSID and password are in secrets/secrets.h  */
#define APP_WIFI_MAX_RETRY      10

/* ── MQTT topics ──────────────────────────── */
#define APP_MQTT_TOPIC_QR_SHOW  "pos/qr/show"
#define APP_MQTT_TOPIC_QR_HIDE  "pos/qr/hide"
#define APP_MQTT_TOPIC_RESULT   "pos/qr/result"

/* ── LCD (ST7701 RGB 480×480 RGB565) ──────── */
#define APP_LCD_H_RES           480
#define APP_LCD_V_RES           480

/* ── LVGL task ────────────────────────────── */
#define APP_LVGL_TICK_MS        1
#define APP_LVGL_TASK_STACK     (6 * 1024)
#define APP_LVGL_TASK_PRIO      2
