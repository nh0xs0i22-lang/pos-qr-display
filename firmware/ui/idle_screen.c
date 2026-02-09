/*
 * Idle screen – flip-clock style HH:MM display.
 *
 * Four dark digit cards with a vertical slide animation when a digit
 * changes, checked once per second but redrawn only on minute change.
 * Time source is the C library clock (time.h); configure it externally
 * via NTP or settimeofday() – until then the display shows 00:00.
 *
 * Layout (centred on 480×480):
 *
 *     ┌────┐ ┌────┐       ┌────┐ ┌────┐
 *     │ H1 │ │ H2 │   :   │ M1 │ │ M2 │
 *     │────│ │────│       │────│ │────│  ← seam line
 *     └────┘ └────┘       └────┘ └────┘
 *
 * Requires LV_FONT_MONTSERRAT_48 enabled in lv_conf.h.
 */

#include "idle_screen.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "app_config.h"

static const char *TAG = "idle_scr";

/* ── Layout ──────────────────────────────────────────────────────────── */

#define CARD_W      90
#define CARD_H      120
#define CARD_R      12          /* border radius                         */
#define PAIR_GAP    8           /* gap between digits within HH / MM     */
#define COLON_GAP   16          /* gap between digit pair and colon      */
#define COLON_W     30          /* space reserved for the colon          */
#define NDIGITS     4           /* H1 H2 M1 M2                          */

#define GROUP_W     (NDIGITS * CARD_W + 2 * PAIR_GAP \
                     + 2 * COLON_GAP + COLON_W)

#define FLIP_MS     350         /* animation duration (ms)               */

/* ── Colours (dark / OLED-style) ─────────────────────────────────────── */

#define COL_BG      lv_color_black()
#define COL_CARD    lv_color_make(0x1C, 0x1C, 0x1C)
#define COL_TEXT    lv_color_white()
#define COL_SEAM    lv_color_make(0x0D, 0x0D, 0x0D)

/* ── Per-digit slot ──────────────────────────────────────────────────── */

typedef struct {
    lv_obj_t *cont;         /* card rectangle – clips children           */
    lv_obj_t *lbl[2];       /* two labels, toggled on each flip          */
    uint8_t   active;       /* index of the currently visible label      */
    char      ch;           /* displayed digit character                 */
} digit_t;

/* ── Module state ────────────────────────────────────────────────────── */

static lv_obj_t   *s_scr;
static digit_t     s_dig[NDIGITS];
static lv_obj_t   *s_colon;
static lv_timer_t *s_timer;
static int         s_last_hour = -1;
static int         s_last_min  = -1;

/* ── Animation helper ────────────────────────────────────────────────── */

static void anim_set_translate_y(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void flip_digit(digit_t *d, char new_ch)
{
    if (d->ch == new_ch) return;

    uint8_t cur  = d->active;
    uint8_t next = 1 - cur;

    /* Place incoming label above the card */
    lv_label_set_text_fmt(d->lbl[next], "%c", new_ch);
    lv_obj_set_style_translate_y(d->lbl[next], -CARD_H, 0);

    /* Outgoing: slide down out of view */
    lv_anim_t a_out;
    lv_anim_init(&a_out);
    lv_anim_set_var(&a_out, d->lbl[cur]);
    lv_anim_set_values(&a_out, 0, CARD_H);
    lv_anim_set_time(&a_out, FLIP_MS);
    lv_anim_set_path_cb(&a_out, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a_out, anim_set_translate_y);
    lv_anim_start(&a_out);

    /* Incoming: slide down into place */
    lv_anim_t a_in;
    lv_anim_init(&a_in);
    lv_anim_set_var(&a_in, d->lbl[next]);
    lv_anim_set_values(&a_in, -CARD_H, 0);
    lv_anim_set_time(&a_in, FLIP_MS);
    lv_anim_set_path_cb(&a_in, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a_in, anim_set_translate_y);
    lv_anim_start(&a_in);

    d->active = next;
    d->ch     = new_ch;
}

/* ── Timer callback ──────────────────────────────────────────────────── *
 * Fires every second, but only triggers animations when the             *
 * minute value actually changes → no continuous redraw.                 */

static void time_check_cb(lv_timer_t *tmr)
{
    (void)tmr;

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    if (t.tm_hour == s_last_hour && t.tm_min == s_last_min) return;

    s_last_hour = t.tm_hour;
    s_last_min  = t.tm_min;

    char buf[5];
    snprintf(buf, sizeof(buf), "%02d%02d", t.tm_hour, t.tm_min);

    for (int i = 0; i < NDIGITS; i++) {
        flip_digit(&s_dig[i], buf[i]);
    }
}

/* ── Widget helpers ──────────────────────────────────────────────────── */

static lv_obj_t *make_digit_label(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    lv_label_set_text_static(lbl, "0");
    lv_obj_center(lbl);
    return lbl;
}

static void init_digit(digit_t *d, lv_obj_t *parent,
                       lv_coord_t x, lv_coord_t y)
{
    /* Card background (children are clipped to its bounds) */
    d->cont = lv_obj_create(parent);
    lv_obj_remove_style_all(d->cont);
    lv_obj_set_size(d->cont, CARD_W, CARD_H);
    lv_obj_set_pos(d->cont, x, y);
    lv_obj_set_style_bg_color(d->cont, COL_CARD, 0);
    lv_obj_set_style_bg_opa(d->cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(d->cont, CARD_R, 0);
    lv_obj_set_style_clip_corner(d->cont, true, 0);
    lv_obj_set_scrollbar_mode(d->cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(d->cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Two labels – alternated on each flip */
    d->lbl[0] = make_digit_label(d->cont);
    d->lbl[1] = make_digit_label(d->cont);
    lv_obj_set_style_translate_y(d->lbl[1], -CARD_H, 0);   /* offscreen */

    /* Horizontal seam (created last → drawn on top of labels) */
    lv_obj_t *seam = lv_obj_create(d->cont);
    lv_obj_remove_style_all(seam);
    lv_obj_set_size(seam, CARD_W, 2);
    lv_obj_align(seam, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(seam, COL_SEAM, 0);
    lv_obj_set_style_bg_opa(seam, LV_OPA_COVER, 0);
    lv_obj_clear_flag(seam, LV_OBJ_FLAG_CLICKABLE);

    d->active = 0;
    d->ch     = '0';
}

/* ── Public API ──────────────────────────────────────────────────────── */

void idle_screen_init(lv_disp_t *disp)
{
    (void)disp;

    /* ── Screen ──────────────────────────────────────────────────── */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    /* ── Digit positions ─────────────────────────────────────────── *
     *                                                                *
     *  |ox| H1 |pg| H2 |cg| : |cg| M1 |pg| M2 |ox|                *
     *       90   8   90  16  30  16  90   8   90                    *
     *  ox = (480 − GROUP_W) / 2                                     */
    const lv_coord_t ox = (APP_LCD_H_RES - GROUP_W) / 2;
    const lv_coord_t oy = (APP_LCD_V_RES - CARD_H) / 2;

    lv_coord_t x = ox;

    /* HH */
    init_digit(&s_dig[0], s_scr, x, oy);   x += CARD_W + PAIR_GAP;
    init_digit(&s_dig[1], s_scr, x, oy);   x += CARD_W + COLON_GAP;

    /* Colon – centred on screen (layout is symmetric) */
    s_colon = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_colon, COL_TEXT, 0);
    lv_obj_set_style_text_font(s_colon, &lv_font_montserrat_48, 0);
    lv_label_set_text_static(s_colon, ":");
    lv_obj_align(s_colon, LV_ALIGN_CENTER, 0, 0);
    x += COLON_W + COLON_GAP;

    /* MM */
    init_digit(&s_dig[2], s_scr, x, oy);   x += CARD_W + PAIR_GAP;
    init_digit(&s_dig[3], s_scr, x, oy);

    /* ── Set initial time without animation ──────────────────────── */
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    s_last_hour = t.tm_hour;
    s_last_min  = t.tm_min;

    char buf[5];
    snprintf(buf, sizeof(buf), "%02d%02d", t.tm_hour, t.tm_min);
    for (int i = 0; i < NDIGITS; i++) {
        lv_label_set_text_fmt(s_dig[i].lbl[0], "%c", buf[i]);
        s_dig[i].ch = buf[i];
    }

    /* 1-second check timer (redraws only when minute changes) */
    s_timer = lv_timer_create(time_check_cb, 1000, NULL);

    ESP_LOGI(TAG, "Idle screen ready (%02d:%02d)", t.tm_hour, t.tm_min);
}

void idle_screen_show(void)
{
    lv_scr_load(s_scr);
    ESP_LOGI(TAG, "Idle screen shown");
}

void idle_screen_hide(void)
{
    /* Timer keeps running – negligible cost, keeps digits current
       so the clock is up-to-date when the screen is shown again. */
    ESP_LOGI(TAG, "Idle screen hidden");
}
