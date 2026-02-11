/*
 * Programmatic rain background – replaces the old C-array image.
 *
 * Draws a moody "rainy window at night" scene using pure LVGL objects:
 *   - Dark navy gradient base
 *   - Bokeh circles (blurry city lights through wet glass)
 *   - Thin vertical rain streaks
 *   - Subtle glass fog overlay
 *
 * No external image data needed.  All objects are static (no animation).
 */

#include "lvgl.h"
#include "bg_rain.h"

void bg_rain_create(lv_obj_t *parent)
{
    /* ── Base: dark gradient background ─────────────────────────────── */
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, 480, 480);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_make(15, 20, 35), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(bg, lv_color_make(25, 30, 45), 0);
    lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Bokeh lights (city lights through rainy window) ───────────── */
    static const struct {
        int16_t x, y, r;
        uint8_t red, grn, blu, opa;
    } lights[] = {
        {120, 200, 40, 255, 200, 100, 30},   /* warm yellow   */
        {350, 180, 55, 255, 150,  50, 25},   /* orange        */
        { 80, 350, 35, 200, 220, 255, 20},   /* cool blue     */
        {400, 320, 45, 255, 180,  80, 28},   /* warm          */
        {240, 280, 50, 255, 255, 200, 22},   /* pale yellow   */
        {180, 150, 30, 200, 150, 255, 18},   /* purple-blue   */
        {320, 380, 38, 255, 200, 150, 25},   /* peach         */
        { 60, 180, 42, 150, 200, 255, 20},   /* sky blue      */
        {440, 150, 35, 255, 220, 100, 22},   /* gold          */
        {280, 100, 48, 200, 180, 255, 18},   /* lavender      */
    };

    for (int i = 0; i < 10; i++) {
        lv_obj_t *dot = lv_obj_create(bg);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, lights[i].r * 2, lights[i].r * 2);
        lv_obj_set_pos(dot, lights[i].x - lights[i].r,
                            lights[i].y - lights[i].r);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_make(lights[i].red,
                                                       lights[i].grn,
                                                       lights[i].blu), 0);
        lv_obj_set_style_bg_opa(dot, lights[i].opa, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ── Rain streaks (thin white vertical lines) ──────────────────── *
     * Deterministic pseudo-random placement — no srand/rand needed.   */
    for (int i = 0; i < 60; i++) {
        lv_obj_t *streak = lv_obj_create(bg);
        lv_obj_remove_style_all(streak);
        int x = (i * 37 + 13) % 480;
        int y = (i * 53 + 7) % 300;
        int h = 40 + (i * 29) % 120;
        lv_obj_set_size(streak, 1, h);
        lv_obj_set_pos(streak, x, y);
        lv_obj_set_style_bg_color(streak, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(streak, 15 + (i * 7) % 30, 0);
        lv_obj_set_style_border_width(streak, 0, 0);
        lv_obj_clear_flag(streak, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ── Subtle glass fog overlay ──────────────────────────────────── */
    lv_obj_t *fog = lv_obj_create(bg);
    lv_obj_remove_style_all(fog);
    lv_obj_set_size(fog, 480, 480);
    lv_obj_set_pos(fog, 0, 0);
    lv_obj_set_style_bg_color(fog, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fog, 8, 0);
    lv_obj_set_style_border_width(fog, 0, 0);
    lv_obj_clear_flag(fog, LV_OBJ_FLAG_SCROLLABLE);
}
