/*
 * ST7701S RGB LCD Driver – 480×480 RGB565
 *
 * Initialises the ST7701S controller via 3-wire SPI (bit-bang),
 * then creates an ESP-IDF RGB panel backed by two PSRAM framebuffers.
 *
 * Pin assignments are board-specific — edit the PIN_* defines below.
 */

#include "lcd_st7701.h"
#include "app_config.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"

static const char *TAG = "st7701";

/* ═══════════════════════════════════════════════════════════════════════ *
 *  Pin definitions – CHANGE THESE to match your board                    *
 * ═══════════════════════════════════════════════════════════════════════ */

/* Backlight (PWM-capable, active-high) */
#define PIN_BL          GPIO_NUM_38

/* 3-wire SPI for ST7701S command interface */
#define PIN_SPI_CS      GPIO_NUM_39
#define PIN_SPI_SCK     GPIO_NUM_48
#define PIN_SPI_SDA     GPIO_NUM_47

/* RGB control signals */
#define PIN_PCLK        GPIO_NUM_21
#define PIN_HSYNC       GPIO_NUM_16
#define PIN_VSYNC       GPIO_NUM_17
#define PIN_DE          GPIO_NUM_18

/* RGB565 data bus (D0–D15) — matched to ESPHome YAML pinout */
#define PIN_D0          GPIO_NUM_7      /* B0 */
#define PIN_D1          GPIO_NUM_15     /* B1 */
#define PIN_D2          GPIO_NUM_8      /* B2 */
#define PIN_D3          GPIO_NUM_20     /* B3 */
#define PIN_D4          GPIO_NUM_3      /* B4 */
#define PIN_D5          GPIO_NUM_13     /* G0 */
#define PIN_D6          GPIO_NUM_14     /* G1 */
#define PIN_D7          GPIO_NUM_0      /* G2 */
#define PIN_D8          GPIO_NUM_4      /* G3 */
#define PIN_D9          GPIO_NUM_5      /* G4 */
#define PIN_D10         GPIO_NUM_6      /* G5 */
#define PIN_D11         GPIO_NUM_46     /* R0 */
#define PIN_D12         GPIO_NUM_9      /* R1 */
#define PIN_D13         GPIO_NUM_10     /* R2 */
#define PIN_D14         GPIO_NUM_11     /* R3 */
#define PIN_D15         GPIO_NUM_12     /* R4 */

/* ═══════════════════════════════════════════════════════════════════════ *
 *  RGB timing — hardware-verified values                                 *
 * ═══════════════════════════════════════════════════════════════════════ */

#define PCLK_HZ             (40 * 1000 * 1000)
#define HSYNC_BACK_PORCH    50
#define HSYNC_FRONT_PORCH   10
#define HSYNC_PULSE_WIDTH   8
#define VSYNC_BACK_PORCH    20
#define VSYNC_FRONT_PORCH   10
#define VSYNC_PULSE_WIDTH   8

/*
 * Bounce buffer: small internal-SRAM region that the GDMA reads from
 * while the CPU refills it from PSRAM in the background.
 * Tune this value to trade SRAM usage for smoother DMA throughput.
 * Larger  → fewer refill interrupts, more SRAM consumed.
 * Smaller → more frequent refills, less SRAM.
 * Set to 0 to disable bounce buffers (direct PSRAM DMA, saves SRAM
 * but may cause flicker if PSRAM bandwidth is contended).
 */
#define BOUNCE_BUF_LINES    10

/* ═══════════════════════════════════════════════════════════════════════ *
 *  3-wire SPI (bit-bang) – used only once during ST7701S register init   *
 * ═══════════════════════════════════════════════════════════════════════ */

static void spi_gpio_init(void)
{
    gpio_set_direction(PIN_SPI_CS,  GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_SPI_SCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_SPI_SDA, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_SPI_CS,  1);
    gpio_set_level(PIN_SPI_SCK, 1);
}

/*
 * Transmit one 9-bit frame over 3-wire SPI:
 *   bit 8  = DC  (0 → command, 1 → parameter)
 *   bit 7…0 = data byte, MSB first
 */
static void spi_write_9bit(bool dc, uint8_t val)
{
    gpio_set_level(PIN_SPI_CS, 0);

    /* DC bit */
    gpio_set_level(PIN_SPI_SCK, 0);
    gpio_set_level(PIN_SPI_SDA, dc);
    gpio_set_level(PIN_SPI_SCK, 1);

    /* D7…D0 */
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_SPI_SCK, 0);
        gpio_set_level(PIN_SPI_SDA, (val >> i) & 1);
        gpio_set_level(PIN_SPI_SCK, 1);
    }

    gpio_set_level(PIN_SPI_CS, 1);
}

static inline void st7701_cmd(uint8_t cmd)   { spi_write_9bit(false, cmd); }
static inline void st7701_data(uint8_t d)    { spi_write_9bit(true,  d);   }

/* ═══════════════════════════════════════════════════════════════════════ *
 *  ST7701S register init sequence (480×480, RGB565)                      *
 * ═══════════════════════════════════════════════════════════════════════ */

static void st7701_panel_init(void)
{
    /* Sleep Out */
    st7701_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* ── Command2 BK0 ───────────────────────────────────────────────── */
    st7701_cmd(0xFF);
    st7701_data(0x77); st7701_data(0x01); st7701_data(0x00);
    st7701_data(0x00); st7701_data(0x10);

    st7701_cmd(0xC0);  /* LNESET  – 480 lines */
    st7701_data(0x3B); st7701_data(0x00);

    st7701_cmd(0xC1);  /* PORCTRL – porch timing */
    st7701_data(0x0D); st7701_data(0x02);

    st7701_cmd(0xC2);  /* INVSET  – inversion & frame rate */
    st7701_data(0x31); st7701_data(0x05);

    st7701_cmd(0xCD);  st7701_data(0x00);

    /* Positive voltage gamma */
    st7701_cmd(0xB0);
    st7701_data(0x00); st7701_data(0x11); st7701_data(0x18); st7701_data(0x0E);
    st7701_data(0x11); st7701_data(0x06); st7701_data(0x07); st7701_data(0x08);
    st7701_data(0x07); st7701_data(0x22); st7701_data(0x04); st7701_data(0x12);
    st7701_data(0x0F); st7701_data(0xAA); st7701_data(0x31); st7701_data(0x18);

    /* Negative voltage gamma */
    st7701_cmd(0xB1);
    st7701_data(0x00); st7701_data(0x11); st7701_data(0x19); st7701_data(0x0E);
    st7701_data(0x12); st7701_data(0x07); st7701_data(0x08); st7701_data(0x08);
    st7701_data(0x08); st7701_data(0x22); st7701_data(0x04); st7701_data(0x11);
    st7701_data(0x11); st7701_data(0xA9); st7701_data(0x32); st7701_data(0x18);

    /* ── Command2 BK1 ───────────────────────────────────────────────── */
    st7701_cmd(0xFF);
    st7701_data(0x77); st7701_data(0x01); st7701_data(0x00);
    st7701_data(0x00); st7701_data(0x11);

    st7701_cmd(0xB0);  st7701_data(0x60);  /* Vop amplitude  */
    st7701_cmd(0xB1);  st7701_data(0x32);  /* VCOM amplitude */
    st7701_cmd(0xB2);  st7701_data(0x07);  /* VGH voltage    */
    st7701_cmd(0xB3);  st7701_data(0x80);
    st7701_cmd(0xB5);  st7701_data(0x49);  /* VGL voltage    */
    st7701_cmd(0xB7);  st7701_data(0x85);
    st7701_cmd(0xB8);  st7701_data(0x21);  /* AVDD / VCL     */
    st7701_cmd(0xC1);  st7701_data(0x78);
    st7701_cmd(0xC2);  st7701_data(0x78);
    st7701_cmd(0xD0);  st7701_data(0x88);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Gate EQ / source EQ */
    st7701_cmd(0xE0);
    st7701_data(0x00); st7701_data(0x1B); st7701_data(0x02);

    st7701_cmd(0xE1);
    st7701_data(0x08); st7701_data(0xA0); st7701_data(0x00); st7701_data(0x00);
    st7701_data(0x07); st7701_data(0xA0); st7701_data(0x00); st7701_data(0x00);
    st7701_data(0x00); st7701_data(0x44); st7701_data(0x44);

    st7701_cmd(0xE2);
    st7701_data(0x11); st7701_data(0x11); st7701_data(0x44); st7701_data(0x44);
    st7701_data(0xED); st7701_data(0xA0); st7701_data(0x00); st7701_data(0x00);
    st7701_data(0xEC); st7701_data(0xA0); st7701_data(0x00); st7701_data(0x00);

    st7701_cmd(0xE3);
    st7701_data(0x00); st7701_data(0x00); st7701_data(0x11); st7701_data(0x11);

    st7701_cmd(0xE4);
    st7701_data(0x44); st7701_data(0x44);

    st7701_cmd(0xE5);
    st7701_data(0x0A); st7701_data(0xE9); st7701_data(0xD8); st7701_data(0xA0);
    st7701_data(0x0C); st7701_data(0xEB); st7701_data(0xD8); st7701_data(0xA0);
    st7701_data(0x0E); st7701_data(0xED); st7701_data(0xD8); st7701_data(0xA0);
    st7701_data(0x10); st7701_data(0xEF); st7701_data(0xD8); st7701_data(0xA0);

    st7701_cmd(0xE6);
    st7701_data(0x00); st7701_data(0x00); st7701_data(0x11); st7701_data(0x11);

    st7701_cmd(0xE7);
    st7701_data(0x44); st7701_data(0x44);

    st7701_cmd(0xE8);
    st7701_data(0x09); st7701_data(0xE8); st7701_data(0xD8); st7701_data(0xA0);
    st7701_data(0x0B); st7701_data(0xEA); st7701_data(0xD8); st7701_data(0xA0);
    st7701_data(0x0D); st7701_data(0xEC); st7701_data(0xD8); st7701_data(0xA0);
    st7701_data(0x0F); st7701_data(0xEE); st7701_data(0xD8); st7701_data(0xA0);

    st7701_cmd(0xEB);
    st7701_data(0x02); st7701_data(0x00); st7701_data(0xE4); st7701_data(0xE4);
    st7701_data(0x88); st7701_data(0x00); st7701_data(0x40);

    st7701_cmd(0xEC);
    st7701_data(0x3C); st7701_data(0x00);

    st7701_cmd(0xED);
    st7701_data(0xAB); st7701_data(0x89); st7701_data(0x76); st7701_data(0x54);
    st7701_data(0x02); st7701_data(0xFF); st7701_data(0xFF); st7701_data(0xFF);
    st7701_data(0xFF); st7701_data(0xFF); st7701_data(0xFF); st7701_data(0x20);
    st7701_data(0x45); st7701_data(0x67); st7701_data(0x98); st7701_data(0xBA);

    /* ── Exit Command2 — back to standard commands ──────────────────── */
    st7701_cmd(0xFF);
    st7701_data(0x77); st7701_data(0x01); st7701_data(0x00);
    st7701_data(0x00); st7701_data(0x00);

    /* COLMOD – 16-bit / pixel (RGB565) */
    st7701_cmd(0x3A);  st7701_data(0x55);

    /* Display ON */
    st7701_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* ═══════════════════════════════════════════════════════════════════════ *
 *  Backlight                                                             *
 * ═══════════════════════════════════════════════════════════════════════ */

static void backlight_set(bool on)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BL,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_BL, on);
}

/* ═══════════════════════════════════════════════════════════════════════ *
 *  lcd_st7701_init                                                       *
 * ═══════════════════════════════════════════════════════════════════════ */

esp_err_t lcd_st7701_init(esp_lcd_panel_handle_t *out_panel)
{
    ESP_RETURN_ON_FALSE(out_panel, ESP_ERR_INVALID_ARG, TAG,
                        "out_panel is NULL");

    /* Backlight off while configuring */
    backlight_set(false);

    /* ST7701S register init over 3-wire SPI */
    spi_gpio_init();
    st7701_panel_init();
    ESP_LOGI(TAG, "ST7701S command init done");

    /* Create the ESP-IDF RGB panel with double PSRAM framebuffers */
    esp_lcd_rgb_panel_config_t rgb_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz           = PCLK_HZ,
            .h_res             = APP_LCD_H_RES,
            .v_res             = APP_LCD_V_RES,
            .hsync_back_porch  = HSYNC_BACK_PORCH,
            .hsync_front_porch = HSYNC_FRONT_PORCH,
            .hsync_pulse_width = HSYNC_PULSE_WIDTH,
            .vsync_back_porch  = VSYNC_BACK_PORCH,
            .vsync_front_porch = VSYNC_FRONT_PORCH,
            .vsync_pulse_width = VSYNC_PULSE_WIDTH,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,   /* RGB565 */
        .num_fbs    = 2,     /* double framebuffer in PSRAM */
        .bounce_buffer_size_px = APP_LCD_H_RES * BOUNCE_BUF_LINES,
        .psram_trans_align     = 64,
        .hsync_gpio_num  = PIN_HSYNC,
        .vsync_gpio_num  = PIN_VSYNC,
        .de_gpio_num     = PIN_DE,
        .pclk_gpio_num   = PIN_PCLK,
        .disp_gpio_num   = -1,
        .data_gpio_nums  = {
            PIN_D0,  PIN_D1,  PIN_D2,  PIN_D3,  PIN_D4,
            PIN_D5,  PIN_D6,  PIN_D7,  PIN_D8,  PIN_D9,
            PIN_D10, PIN_D11, PIN_D12, PIN_D13, PIN_D14, PIN_D15,
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&rgb_cfg, &panel),
                        TAG, "RGB panel creation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel),
                        TAG, "RGB panel init failed");

    backlight_set(true);

    ESP_LOGI(TAG, "LCD ready (%dx%d RGB565, 2× PSRAM framebuffer)",
             APP_LCD_H_RES, APP_LCD_V_RES);

    *out_panel = panel;
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════ *
 *  LVGL registration – direct-mode, VSYNC-synchronised flush             *
 * ═══════════════════════════════════════════════════════════════════════ */

static SemaphoreHandle_t s_vsync_sem;

static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *edata,
                               void *user_ctx)
{
    BaseType_t yield = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync_sem, &yield);
    return (yield == pdTRUE);
}

/*
 * Flush callback for LVGL.
 *
 * In direct mode the colour buffer IS one of the two PSRAM framebuffers.
 * Calling draw_bitmap with that pointer triggers a fast pointer swap
 * inside the RGB panel driver (no pixel copy).  We then wait for VSYNC
 * so the swap takes effect before LVGL starts drawing into the now-
 * inactive back-buffer.
 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = drv->user_data;

    esp_lcd_panel_draw_bitmap(panel, 0, 0,
                              APP_LCD_H_RES, APP_LCD_V_RES, color_map);

    xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(100));

    lv_disp_flush_ready(drv);
}

esp_err_t lcd_st7701_register_lvgl(esp_lcd_panel_handle_t panel,
                                   lv_disp_t **out_disp)
{
    ESP_RETURN_ON_FALSE(panel && out_disp, ESP_ERR_INVALID_ARG, TAG,
                        "NULL argument");

    /* Binary semaphore for VSYNC synchronisation */
    s_vsync_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_vsync_sem, ESP_ERR_NO_MEM, TAG,
                        "vsync semaphore alloc failed");

    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    ESP_RETURN_ON_ERROR(
        esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, NULL),
        TAG, "register vsync callback failed");

    /* Obtain the two PSRAM framebuffer addresses (zero-copy) */
    void *fb0 = NULL;
    void *fb1 = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1),
        TAG, "get frame buffer failed");

    /* LVGL draw buffer pair — points straight at the PSRAM framebuffers */
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, fb0, fb1,
                          APP_LCD_H_RES * APP_LCD_V_RES);

    /* LVGL display driver */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res     = APP_LCD_H_RES;
    disp_drv.ver_res     = APP_LCD_V_RES;
    disp_drv.flush_cb    = lvgl_flush_cb;
    disp_drv.draw_buf    = &draw_buf;
    disp_drv.user_data   = panel;
    disp_drv.direct_mode = true;

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "lv_disp_drv_register failed");

    ESP_LOGI(TAG, "LVGL display registered (direct mode, double-buffered)");

    *out_disp = disp;
    return ESP_OK;
}
