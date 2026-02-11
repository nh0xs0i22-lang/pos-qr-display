/*
 * Glassmorphism Flip Clock UI
 *
 * Rain-themed idle screen for 480×480 RGB display:
 *   - Full-screen pre-darkened rain background image (bg_rain)
 *   - "MK BEAUTY HOUSE" header with decorative line
 *   - HH:MM:SS glass-card flip clock with colon blink
 *   - Rotating Vietnamese quotes with cross-fade
 *   - Full-screen tap → static VietQR
 *
 * Requires:
 *   bg_rain.c      – 480×480 LVGL C-array image (CF_TRUE_COLOR).
 *   font_vietnam_20 – custom Vietnamese font (20 px).
 *   LV_FONT_MONTSERRAT_48 enabled in lv_conf.h / sdkconfig.
 */

#include "ui.h"
#include "qr_screen.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "app_config.h"

static const char *TAG = "ui";

/* ── External assets ─────────────────────────────────────────────────────── */
#include "bg_rain.h"
LV_FONT_DECLARE(font_vietnam_20);

/* ── Layout ──────────────────────────────────────────────────────────────── */

#define CARD_W      64
#define CARD_H      90
#define CARD_R      14          /* border radius                             */
#define ITEM_GAP    6           /* uniform flex gap between clock items      */
#define NDIGITS     6           /* H1 H2 M1 M2 S1 S2                        */

#define FLIP_MS         350     /* digit slide animation (ms)                */
#define QUOTE_ROTATE_S  10      /* rotate quote every N seconds              */
#define QUOTE_FADE_MS   500     /* cross-fade duration (ms)                  */

/* ── Colours (glassmorphism palette) ─────────────────────────────────────── */

#define COL_CARD_BG         lv_color_black()
#define COL_CARD_BG_OPA     LV_OPA_40
#define COL_CARD_BORDER     lv_color_white()
#define COL_CARD_BORDER_OPA LV_OPA_20
#define COL_DIGIT           lv_color_white()
#define COL_COLON           lv_color_white()
#define COL_HEADER          lv_color_white()
#define COL_DECO_LINE       lv_color_white()
#define COL_QUOTE           lv_color_make(0xBB, 0xBB, 0xBB)

/* ── Per-digit slot ──────────────────────────────────────────────────────── */

typedef struct {
    lv_obj_t *card;
    lv_obj_t *lbl[2];          /* two labels, toggled on each flip           */
    uint8_t   active;          /* index of the currently visible label       */
    char      ch;              /* displayed digit character                  */
} digit_t;

/* ── Vietnamese quotes ───────────────────────────────────────────────────── */

static const char *QUOTES[] = {
    "Yêu bản thân là khởi đầu của hạnh phúc",
    "Phụ nữ đẹp nhất khi là chính mình",
    "Thư giãn - Nghỉ ngơi - Hồi phục",
    "Chăm sóc bản thân là ưu tiên hàng đầu",
    "Vẻ đẹp đến từ sự bình yên nội tâm",
    "Đừng quên mỉm cười ngày hôm nay",
    "Bạn xứng đáng được yêu thương",
    "MK Beauty House - Nơi vẻ đẹp thăng hoa",
    "Hạnh phúc là được làm điều mình yêu",
    "Mỗi người phụ nữ là một món quà vô giá",
    "Yêu chiều bản thân không phải là ích kỷ",
    "Vẻ đẹp bắt đầu từ khoảnh khắc bạn là chính mình",
    "Đôi tay đẹp làm nên những điều kỳ diệu",
    "Sống chậm lại và yêu thương nhiều hơn",
    "Nụ cười là trang sức lấp lánh nhất",
    "Hãy để chúng tôi chăm sóc bạn",
    "Thư thái tâm hồn, rạng ngời nhan sắc",
    "Đẹp hơn mỗi ngày cùng MK Beauty"
};
#define NUM_QUOTES  (sizeof(QUOTES) / sizeof(QUOTES[0]))

/* ── Module state ────────────────────────────────────────────────────────── */

static lv_obj_t   *s_scr;
static digit_t     s_dig[NDIGITS];
static lv_obj_t   *s_colon[2];        /* HH:MM and MM:SS colons             */
static lv_obj_t   *s_lbl_quote;
static lv_timer_t *s_timer;

static int      s_last_hour = -1;
static int      s_last_min  = -1;
static int      s_last_sec  = -1;
static uint32_t s_tick_count;          /* 500 ms ticks since last quote      */
static int      s_quote_idx;

/* ── Animation helpers ───────────────────────────────────────────────────── */

static void anim_set_y(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void anim_set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

/* ── Digit flip animation ────────────────────────────────────────────────── */

static void flip_digit(digit_t *d, char new_ch)
{
    if (d->ch == new_ch) return;

    uint8_t cur  = d->active;
    uint8_t next = 1 - cur;

    lv_label_set_text_fmt(d->lbl[next], "%c", new_ch);
    lv_obj_set_style_translate_y(d->lbl[next], -CARD_H, 0);

    lv_anim_t a;

    /* Outgoing label: slide down out of view */
    lv_anim_init(&a);
    lv_anim_set_var(&a, d->lbl[cur]);
    lv_anim_set_values(&a, 0, CARD_H);
    lv_anim_set_time(&a, FLIP_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_start(&a);

    /* Incoming label: slide down into place */
    lv_anim_init(&a);
    lv_anim_set_var(&a, d->lbl[next]);
    lv_anim_set_values(&a, -CARD_H, 0);
    lv_anim_set_time(&a, FLIP_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_start(&a);

    d->active = next;
    d->ch     = new_ch;
}

/* ── Quote cross-fade ────────────────────────────────────────────────────── */

static void quote_fade_in(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_lbl_quote);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, QUOTE_FADE_MS / 2);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    lv_anim_start(&a);
}

static void quote_fade_out_done(lv_anim_t *a)
{
    (void)a;
    s_quote_idx = (s_quote_idx + 1) % NUM_QUOTES;
    lv_label_set_text_static(s_lbl_quote, QUOTES[s_quote_idx]);
    quote_fade_in();
}

static void quote_rotate(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_lbl_quote);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, QUOTE_FADE_MS / 2);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    lv_anim_set_ready_cb(&a, quote_fade_out_done);
    lv_anim_start(&a);
}

/* ── Timer callback (500 ms) ─────────────────────────────────────────────── */

static void ui_timer_cb(lv_timer_t *tmr)
{
    (void)tmr;

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    /* Update digits when any field changes */
    if (t.tm_hour != s_last_hour || t.tm_min != s_last_min ||
        t.tm_sec  != s_last_sec) {
        s_last_hour = t.tm_hour;
        s_last_min  = t.tm_min;
        s_last_sec  = t.tm_sec;

        char buf[7];
        snprintf(buf, sizeof(buf), "%02d%02d%02d",
                 t.tm_hour, t.tm_min, t.tm_sec);
        for (int i = 0; i < NDIGITS; i++)
            flip_digit(&s_dig[i], buf[i]);
    }

    /* Blink colons (toggle every 500 ms) */
    static bool colon_vis = true;
    colon_vis = !colon_vis;
    lv_opa_t opa = colon_vis ? LV_OPA_COVER : LV_OPA_30;
    lv_obj_set_style_text_opa(s_colon[0], opa, 0);
    lv_obj_set_style_text_opa(s_colon[1], opa, 0);

    /* Rotate quote every QUOTE_ROTATE_S seconds */
    s_tick_count++;
    if (s_tick_count >= (QUOTE_ROTATE_S * 2)) {     /* ×2 because 500 ms */
        s_tick_count = 0;
        quote_rotate();
    }
}

/* ── Widget builders ─────────────────────────────────────────────────────── */

static lv_obj_t *make_glass_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_W, CARD_H);

    lv_obj_set_style_bg_color(card,     COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card,       COL_CARD_BG_OPA, 0);
    lv_obj_set_style_radius(card,       CARD_R, 0);
    lv_obj_set_style_border_color(card, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_opa(card,   COL_CARD_BORDER_OPA, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_clip_corner(card,  true, 0);
    lv_obj_set_scrollbar_mode(card,     LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    return card;
}

static lv_obj_t *make_digit_label(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, COL_DIGIT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text_static(lbl, "0");
    lv_obj_center(lbl);
    return lbl;
}

static void init_digit(digit_t *d, lv_obj_t *parent)
{
    d->card   = make_glass_card(parent);
    d->lbl[0] = make_digit_label(d->card);
    d->lbl[1] = make_digit_label(d->card);
    lv_obj_set_style_translate_y(d->lbl[1], -CARD_H, 0);
    d->active = 0;
    d->ch     = '0';
}

static lv_obj_t *make_colon_label(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, COL_COLON, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text_static(lbl, ":");
    return lbl;
}

/* ── Static VietQR (walk-in / tip payments) ──────────────────────────────── *
 *                                                                            *
 * EMVCo QR payload for MB Bank (BIN 970422), account 0973202625.            *
 * CRC-16/CCITT-FALSE is computed at runtime and appended to tag 63.         *
 * ────────────────────────────────────────────────────────────────────────── */

static const char VIETQR_BASE[] =
    "00020101021138540010A000000727"
    "0124000697042201100973202625"
    "0208QRIBFTTA"
    "5303704"
    "5802VN"
    "62230819Thanh toan tai quay"
    "6304";

static char s_vietqr[128];

static uint16_t crc16_ccitt(const char *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)(uint8_t)data[i] << 8);
        for (int j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
    return crc;
}

static void on_idle_tap(lv_event_t *e)
{
    (void)e;
    qr_screen_show_static(s_vietqr, "",
                           "NGUYEN THI NHI - MB Bank");
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ui_init(lv_disp_t *disp)
{
    (void)disp;

    /* ── Screen ──────────────────────────────────────────────────────── */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Rain background (programmatic — no image file needed) ────────── */
    bg_rain_create(s_scr);

    /* ── Header: "MK BEAUTY HOUSE" ───────────────────────────────────── */
    lv_obj_t *hdr = lv_label_create(s_scr);
    lv_obj_set_style_text_color(hdr, COL_HEADER, 0);
    lv_obj_set_style_text_font(hdr, &font_vietnam_20, 0);
    lv_obj_set_style_text_letter_space(hdr, 4, 0);
    lv_label_set_text_static(hdr, "MK BEAUTY HOUSE");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 46);

    /* Decorative line under header */
    lv_obj_t *deco = lv_obj_create(s_scr);
    lv_obj_remove_style_all(deco);
    lv_obj_set_size(deco, 160, 1);
    lv_obj_set_style_bg_color(deco, COL_DECO_LINE, 0);
    lv_obj_set_style_bg_opa(deco, LV_OPA_40, 0);
    lv_obj_align_to(deco, hdr, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    /* ── Clock row (flex container) ──────────────────────────────────── *
     *  H1 H2 : M1 M2 : S1 S2                                          *
     *  6 glass cards + 2 colon labels, uniform gap                     */
    lv_obj_t *row = lv_obj_create(s_scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ITEM_GAP, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -10);

    init_digit(&s_dig[0], row);          /* H1 */
    init_digit(&s_dig[1], row);          /* H2 */
    s_colon[0] = make_colon_label(row);  /* :  */
    init_digit(&s_dig[2], row);          /* M1 */
    init_digit(&s_dig[3], row);          /* M2 */
    s_colon[1] = make_colon_label(row);  /* :  */
    init_digit(&s_dig[4], row);          /* S1 */
    init_digit(&s_dig[5], row);          /* S2 */

    /* ── Vietnamese quote ────────────────────────────────────────────── */
    s_lbl_quote = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_lbl_quote, COL_QUOTE, 0);
    lv_obj_set_style_text_font(s_lbl_quote, &font_vietnam_20, 0);
    lv_obj_set_style_text_align(s_lbl_quote, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_quote, 420);
    lv_label_set_long_mode(s_lbl_quote, LV_LABEL_LONG_WRAP);
    s_quote_idx = 0;
    lv_label_set_text_static(s_lbl_quote, QUOTES[0]);
    lv_obj_align(s_lbl_quote, LV_ALIGN_BOTTOM_MID, 0, -55);

    /* ── Set initial time without animation ──────────────────────────── */
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    s_last_hour = t.tm_hour;
    s_last_min  = t.tm_min;
    s_last_sec  = t.tm_sec;

    char buf[7];
    snprintf(buf, sizeof(buf), "%02d%02d%02d",
             t.tm_hour, t.tm_min, t.tm_sec);
    for (int i = 0; i < NDIGITS; i++) {
        lv_label_set_text_fmt(s_dig[i].lbl[0], "%c", buf[i]);
        s_dig[i].ch = buf[i];
    }

    /* ── 500 ms timer (clock + colon blink + quote rotation) ─────────── */
    s_tick_count = 0;
    s_timer = lv_timer_create(ui_timer_cb, 500, NULL);

    /* ── Build static VietQR string (one-time) ───────────────────────── */
    strcpy(s_vietqr, VIETQR_BASE);
    uint16_t crc = crc16_ccitt(s_vietqr, strlen(s_vietqr));
    snprintf(s_vietqr + strlen(s_vietqr), 5, "%04X", crc);

    /* ── Full-screen tap overlay (triggers static VietQR) ────────────── */
    lv_obj_t *overlay = lv_obj_create(s_scr);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, APP_LCD_H_RES, APP_LCD_V_RES);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, on_idle_tap, LV_EVENT_CLICKED, NULL);

    /* ── Make this screen active ─────────────────────────────────────── */
    lv_scr_load(s_scr);

    ESP_LOGI(TAG, "UI ready (%02d:%02d:%02d)",
             t.tm_hour, t.tm_min, t.tm_sec);
}
