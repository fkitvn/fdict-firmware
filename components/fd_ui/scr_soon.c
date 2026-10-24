/* Man tam "Sap co" cho Hoc tap / Dong bo / Thong ke (khong header, vuot mep de quay lai). */
#include "fd_fonts.h"
#include "ui_priv.h"

static void build_soon(lv_obj_t *scr, const char *icon, const char *title)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *i = lv_label_create(body);
    lv_obj_set_style_text_font(i, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(i, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_text(i, icon);
    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(t, title);
    lv_obj_t *s = lv_label_create(body);
    lv_obj_set_width(s, lv_pct(100)); lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(s, "Sắp có");
    lv_obj_t *h = lv_label_create(body);
    lv_obj_set_style_text_color(h, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(h, "Vuốt từ mép trái/phải để quay lại");
}

static void build_sync(lv_obj_t *scr) { build_soon(scr, LV_SYMBOL_REFRESH, "Đồng bộ"); }
static void build_stats(lv_obj_t *scr) { build_soon(scr, LV_SYMBOL_IMAGE, "Thống kê"); }
const ui_screen_t g_scr_sync = {.name = "sync", .parent = FD_SCR_HOME, .build = build_sync};
const ui_screen_t g_scr_stats = {.name = "stats", .parent = FD_SCR_HOME, .build = build_stats};
