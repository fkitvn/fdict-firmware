/* Cai dat > Do sang man hinh: gia tri lon, nut [-] [+] (buoc 10%) va thanh truot 10..100%.
 * Ap dung ngay (LEDC GPIO45 qua fd_display), luu NVS fd/bright khi nha tay.
 * Le ngang 34px > vung vuot mep 26px -> keo thanh truot khong kich hoat quay lai. */
#include "fd_display.h"
#include "fd_fonts.h"
#include "ui_priv.h"

#define STEP 10
static lv_obj_t *s_val, *s_slider;

static void show(int v)
{
    lv_label_set_text_fmt(s_val, "%d%%", v);
    lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
}

static void set_level(int v, bool save)
{
    fd_display_set_brightness(v);
    show(fd_display_get_brightness());
    if (save) fd_display_save_brightness();
}

static void btn_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int v = (fd_display_get_brightness() + d) / STEP * STEP;   /* bam theo boi so 10 */
    set_level(v, true);
}

static void slider_cb(lv_event_t *e)
{
    set_level((int)lv_slider_get_value(s_slider), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

static lv_obj_t *big_btn(lv_obj_t *parent, const char *sym, int delta)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? 48 : 64, 52);   /* doc: nut hep hon cho vua hang */
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_mont_32, 0);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)delta);
    return b;
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_hor(body, (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? 8 : 34, 0);   /* doc 240 px: 34 px le lam hang -/gia tri/+ tran */
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* doc: tieu de xuong dong, khong tran */
    lv_label_set_text(t, _("SCREEN_BRIGHTNESS"));

    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    big_btn(row, LV_SYMBOL_MINUS, -STEP);
    s_val = lv_label_create(row);
    lv_obj_set_style_text_font(s_val, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(s_val, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    big_btn(row, LV_SYMBOL_PLUS, STEP + STEP - 1);   /* +19 roi bam xuong boi so 10 = +10 (hoac len moc 10 ke tiep) */

    s_slider = lv_slider_create(body);
    lv_obj_set_width(s_slider, lv_pct(100));
    lv_obj_set_height(s_slider, 14);
    lv_slider_set_range(s_slider, 10, 100);
    lv_obj_set_ext_click_area(s_slider, 14);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_RELEASED, NULL);
    show(fd_display_get_brightness());
}

static void destroy(void) { s_val = s_slider = NULL; }

const ui_screen_t g_scr_brightness = {
    .name = "brightness", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};