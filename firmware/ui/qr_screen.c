/*
 * QR display screen – shows QR code with optional amount and description.
 *
 * Layout (480×480):
 *   ┌─────────────────────────────┐
 *   │         [desc text]         │
 *   │                             │
 *   │      ┌──────────────┐      │
 *   │      │   QR  CODE   │      │
 *   │      │   280×280    │      │
 *   │      └──────────────┘      │
 *   │                             │
 *   │       [amount text]         │
 *   └─────────────────────────────┘
 *
 * All LVGL objects are created once in qr_screen_init() and reused.
 */

#include "qr_screen.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "qr_scr";

#define QR_SIZE     280

/* ── Static widget handles (created once, reused) ────────────────────── */

static lv_obj_t *s_scr_idle;       /* default idle screen (white)       */
static lv_obj_t *s_scr_qr;         /* QR display screen                 */
static lv_obj_t *s_qr;             /* QR code widget                    */
static lv_obj_t *s_lbl_amount;     /* amount label (below QR)           */
static lv_obj_t *s_lbl_desc;       /* description label (above QR)      */

/* Snapshot of the last payload passed to show().
   Used to skip redundant lv_qrcode_update() calls. */
static qr_payload_t s_last;

/* User-dismiss flag: true = user tapped to hide QR, suppress auto-show. */
static bool s_qr_dismissed_by_user;

/* True while a non-MQTT (static) QR is being displayed. */
static bool s_showing_static;

static void on_qr_screen_tap(lv_event_t *e)
{
    (void)e;
    if (s_showing_static) {
        /* Static QR: just return to idle, no MQTT dismiss flag. */
        s_showing_static = false;
        qr_screen_hide();
        ESP_LOGI(TAG, "Static QR dismissed");
    } else {
        /* MQTT QR: set dismiss flag so the main loop won't re-show. */
        s_qr_dismissed_by_user = true;
        qr_screen_hide();
        ESP_LOGI(TAG, "QR dismissed by user");
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void qr_screen_init(lv_disp_t *disp)
{
    /* ── Idle screen: plain white ─────────────────────────────────── */
    s_scr_idle = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(s_scr_idle, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr_idle, LV_OPA_COVER, 0);

    /* ── QR screen ────────────────────────────────────────────────── */
    s_scr_qr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_qr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_scr_qr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_scr_qr, on_qr_screen_tap, LV_EVENT_CLICKED, NULL);

    /* QR code widget – centred, no border */
    s_qr = lv_qrcode_create(s_scr_qr, QR_SIZE,
                             lv_color_black(), lv_color_white());
    lv_obj_center(s_qr);
    lv_obj_set_style_border_width(s_qr, 0, 0);

    /* Amount label – below QR */
    s_lbl_amount = lv_label_create(s_scr_qr);
    lv_obj_set_style_text_color(s_lbl_amount, lv_color_black(), 0);
    lv_obj_set_style_text_align(s_lbl_amount, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_amount, 440);
    lv_label_set_text_static(s_lbl_amount, "");
    lv_obj_align_to(s_lbl_amount, s_qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    /* Description label – above QR */
    s_lbl_desc = lv_label_create(s_scr_qr);
    lv_obj_set_style_text_color(s_lbl_desc,
                                lv_color_make(0x60, 0x60, 0x60), 0);
    lv_obj_set_style_text_align(s_lbl_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_desc, 440);
    lv_label_set_text_static(s_lbl_desc, "");
    lv_obj_align_to(s_lbl_desc, s_qr, LV_ALIGN_OUT_TOP_MID, 0, -12);

    ESP_LOGI(TAG, "QR screen ready");
}

void qr_screen_show(const qr_payload_t *payload)
{
    s_showing_static = false;   /* MQTT path clears static flag */

    /* Skip update if payload is identical to the last one shown. */
    if (memcmp(&s_last, payload, sizeof(s_last)) == 0) {
        /* Ensure QR screen is active even if data unchanged. */
        if (lv_scr_act() != s_scr_qr) {
            lv_scr_load(s_scr_qr);
        }
        return;
    }
    s_last = *payload;

    /* Update QR code content */
    lv_qrcode_update(s_qr, payload->data, strlen(payload->data));

    /* Update text labels (empty string hides the label visually) */
    lv_label_set_text(s_lbl_amount, payload->amount);
    lv_label_set_text(s_lbl_desc,   payload->desc);

    /* Re-align after text change */
    lv_obj_align_to(s_lbl_amount, s_qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    lv_obj_align_to(s_lbl_desc,   s_qr, LV_ALIGN_OUT_TOP_MID,    0, -12);

    lv_scr_load(s_scr_qr);

    ESP_LOGI(TAG, "Showing QR  amount=\"%s\"  desc=\"%s\"",
             payload->amount, payload->desc);
}

void qr_screen_show_static(const char *qr_data, const char *amount,
                            const char *desc)
{
    qr_payload_t payload = {0};
    strncpy(payload.data,   qr_data, sizeof(payload.data)   - 1);
    strncpy(payload.amount, amount,  sizeof(payload.amount)  - 1);
    strncpy(payload.desc,   desc,    sizeof(payload.desc)    - 1);

    qr_screen_show(&payload);   /* reuse existing render path */
    s_showing_static = true;    /* override: this is not MQTT */
    ESP_LOGI(TAG, "Showing static QR");
}

void qr_screen_hide(void)
{
    lv_scr_load(s_scr_idle);
    memset(&s_last, 0, sizeof(s_last));
    s_showing_static = false;
    ESP_LOGI(TAG, "QR hidden");
}

bool qr_screen_is_dismissed(void)
{
    return s_qr_dismissed_by_user;
}

void qr_screen_clear_dismissed(void)
{
    s_qr_dismissed_by_user = false;
}
