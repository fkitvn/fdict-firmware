/* Cai dat > Am luong: gia tri lon (+ bieu tuong loa / tat tieng o 0%), nut [-] [+] (buoc 10%), thanh truot 0..100%.
 * Ap dung + luu NVS fd/vol khi bam nut / nha thanh truot, phat chuong xem truoc (bo qua o 0%).
 * Le ngang 34px > vung vuot mep 26px -> keo thanh truot khong kich hoat quay lai. */
#include "fd_audio.h"
#include "fd_fonts.h"
#include "ui_priv.h"

#define STEP 10
static lv_obj_t *s_val, *s_icon, *s_slider;

static void show(int v)
{
    lv_label_set_text_fmt(s_val, "%d%%", v);
    lv_label_set_text(s_icon, v == 0 ? LV_SYMBOL_MUTE : v < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(s_icon, lv_color_hex(v == 0 ? FD_COLOR_TEXT_MUTED : FD_COLOR_PRIMARY_TEXT), 0);
    lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
}

static void apply(int v)
{
    fd_audio_set_volume(v);
    v = fd_audio_get_volume();
    show(v);
    if (v > 0) ui_vol_beep();   /* 'bip bip' o muc moi (thanh truot) */
}

static void btn_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int v = fd_audio_get_volume();
    v = d > 0 ? (v / STEP + 1) * STEP : (v + STEP - 1) / STEP * STEP - STEP;   /* bam theo boi so 10 */
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    fd_audio_set_volume(v);
    v = fd_audio_get_volume();
    show(v);
    if (v > 0) ui_vol_beep();   /* 'bip bip' o muc moi; am luong 0 -> im */
}

static void slider_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(s_slider);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) apply(v);
    else lv_label_set_text_fmt(s_val, "%d%%", v);   /* dang keo: chi cap nhat so */
}

static void big_btn(lv_obj_t *parent, const char *sym, int delta)
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
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_hor(body, (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? 8 : 34, 0);   /* doc 240 px: 34 px le lam hang -/loa+gia tri/+ tran */
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* doc: tieu de xuong dong, khong tran */
    lv_label_set_text(t, "Âm lượng");

    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    big_btn(row, LV_SYMBOL_MINUS, -1);
    lv_obj_t *mid = lv_obj_create(row);
    lv_obj_remove_style_all(mid);
    lv_obj_set_size(mid, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mid, (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? 4 : 8, 0);
    s_icon = lv_label_create(mid);
    lv_obj_set_style_text_font(s_icon, &fd_font_mont_32, 0);
    s_val = lv_label_create(mid);
    lv_obj_set_style_text_font(s_val, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(s_val, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    big_btn(row, LV_SYMBOL_PLUS, 1);

    s_slider = lv_slider_create(body);
    lv_obj_set_width(s_slider, lv_pct(100));
    lv_obj_set_height(s_slider, 14);
    lv_slider_set_range(s_slider, 0, 100);
    lv_obj_set_ext_click_area(s_slider, 14);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_RELEASED, NULL);
    show(fd_audio_get_volume());
}

static void destroy(void) { s_val = s_icon = s_slider = NULL; }

const ui_screen_t g_scr_volume = {
    .name = "volume", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};