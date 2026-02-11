/*
 * GT911 capacitive touch driver for Guition ESP32-S3-4848S040.
 *
 * Uses ESP-IDF 5.x new I2C master driver.
 * I2C pins: SDA=GPIO 19, SCL=GPIO 45, address=0x5D.
 * No INT or RST pin on this board.
 *
 * Diagnostic layers:
 *   Layer A – raw I2C touch data (logged on every touch event)
 *   Layer B – LVGL indev read_cb state (logged on every touch event)
 */

#include "touch_gt911.h"
#include "app_config.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "gt911";

/* ── GT911 register addresses ─────────────────────────────────────────── */

#define GT911_REG_STATUS    0x814E   /* bit7=ready, bits3:0=touch count   */
#define GT911_REG_POINT0    0x8150   /* first touch point (8 bytes)       */
#define GT911_REG_PRODUCT   0x8140   /* product ID (4 bytes, ASCII)       */

/* ── Static handles ───────────────────────────────────────────────────── */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static lv_indev_t             *s_indev;

/* ── I2C register helpers ─────────────────────────────────────────────── */

static esp_err_t gt911_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, addr, sizeof(addr),
                                       buf, len, 50);
}

static esp_err_t gt911_write_reg(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[2 + 8];   /* max write: 2-byte addr + up to 8 data bytes */
    if (len > 8) return ESP_ERR_INVALID_SIZE;
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(&buf[2], data, len);
    return i2c_master_transmit(s_dev, buf, 2 + len, 50);
}

static esp_err_t gt911_clear_status(void)
{
    uint8_t zero = 0;
    return gt911_write_reg(GT911_REG_STATUS, &zero, 1);
}

/* ── LVGL read callback ───────────────────────────────────────────────── */

static void gt911_lvgl_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    data->state = LV_INDEV_STATE_RELEASED;

    uint8_t status = 0;
    esp_err_t err = gt911_read_reg(GT911_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return;
    }

    bool ready = (status & 0x80) != 0;
    uint8_t touches = status & 0x0F;

    if (!ready || touches == 0) {
        if (ready) {
            gt911_clear_status();
        }
        return;
    }

    /* Read first touch point: track_id(1) + x(2) + y(2) + size(2) + reserved(1) */
    uint8_t pt[8] = {0};
    err = gt911_read_reg(GT911_REG_POINT0, pt, sizeof(pt));
    gt911_clear_status();

    if (err != ESP_OK) {
        return;
    }

    uint16_t x = (uint16_t)pt[1] | ((uint16_t)pt[2] << 8);
    uint16_t y = (uint16_t)pt[3] | ((uint16_t)pt[4] << 8);

    /* Layer A: raw touch data */
    ESP_LOGI(TAG, "[A] raw: touches=%d x=%u y=%u size=%u",
             touches, x, y, (uint16_t)pt[5] | ((uint16_t)pt[6] << 8));

    /* Clamp to display resolution */
    if (x >= APP_LCD_H_RES) x = APP_LCD_H_RES - 1;
    if (y >= APP_LCD_V_RES) y = APP_LCD_V_RES - 1;

    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state   = LV_INDEV_STATE_PRESSED;

    /* Layer B: LVGL indev state */
    ESP_LOGI(TAG, "[B] indev: state=PRESSED x=%d y=%d",
             data->point.x, data->point.y);
}

/* ── Public API ───────────────────────────────────────────────────────── */

esp_err_t touch_gt911_init(void)
{
    ESP_LOGI(TAG, "Initialising GT911 (SDA=%d SCL=%d addr=0x%02X)",
             APP_TOUCH_I2C_SDA, APP_TOUCH_I2C_SCL, APP_TOUCH_GT911_ADDR);

    /* 1. Create I2C master bus */
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port        = I2C_NUM_0,
        .sda_io_num      = APP_TOUCH_I2C_SDA,
        .scl_io_num      = APP_TOUCH_I2C_SCL,
        .clk_source      = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus create failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 2. Probe GT911 address */
    err = i2c_master_probe(s_bus, APP_TOUCH_GT911_ADDR, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 not found at 0x%02X: %s",
                 APP_TOUCH_GT911_ADDR, esp_err_to_name(err));
        /* Try alternate address 0x14 */
        err = i2c_master_probe(s_bus, 0x14, 100);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "GT911 found at alternate address 0x14!");
        } else {
            ESP_LOGE(TAG, "GT911 not found at 0x14 either: %s",
                     esp_err_to_name(err));
            return ESP_ERR_NOT_FOUND;
        }
    }
    ESP_LOGI(TAG, "GT911 probe OK at 0x%02X", APP_TOUCH_GT911_ADDR);

    /* 3. Add GT911 as I2C device */
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = APP_TOUCH_GT911_ADDR,
        .scl_speed_hz    = APP_TOUCH_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 4. Read product ID for verification */
    uint8_t product[4] = {0};
    err = gt911_read_reg(GT911_REG_PRODUCT, product, 4);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "GT911 product ID: %c%c%c%c",
                 product[0], product[1], product[2], product[3]);
    } else {
        ESP_LOGW(TAG, "Could not read product ID: %s", esp_err_to_name(err));
    }

    /* 5. Clear any pending touch status */
    gt911_clear_status();

    ESP_LOGI(TAG, "GT911 init OK");
    return ESP_OK;
}

esp_err_t touch_gt911_register_lvgl(void)
{
    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type    = LV_INDEV_TYPE_POINTER;
    drv.read_cb = gt911_lvgl_read_cb;

    s_indev = lv_indev_drv_register(&drv);
    if (!s_indev) {
        ESP_LOGE(TAG, "Failed to register LVGL indev");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL pointer indev registered");
    return ESP_OK;
}
