/* 4.3.3: man chon bo tu dung chung 5 module hoc.
 * 3 nut chon mot (radio): Tat ca, Tu moi them, Ket hop. O danh dau (checkbox) danh sach bo tu (toi da 32).
 * Luu NVS fdsync. Quay lai (vuot mep / giu B1) ve module vua mo man nay (lich su dieu huong). */
#include "fd_fonts.h"
#include "fd_sync.h"
#include "ui_priv.h"

static const char *const MD_KEYS[] = {"ALL", "NEW_ADDED", "COMBINED"};

static void refresh_async(void *arg)
{
    (void)arg;
    ui_nav_refresh();
}

static void mode_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    fd_sync_pick_set_mode((fd_sync_pick_mode_t)v);
    lv_async_call(refresh_async, NULL);
}

static void col_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    fd_sync_pick_toggle_col(i);
    lv_async_call(refresh_async, NULL);
}

static lv_obj_t *row_btn(lv_obj_t *p)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, 40);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 10, 0);
    return b;
}

/* Nut chon mot: vong tron, cham trang o giua khi dang chon. */
static void radio_mark(lv_obj_t *b, bool on)
{
    lv_obj_t *o = lv_obj_create(b);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 22, 22);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, on ? 0 : 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
    if (on) {
        lv_obj_t *dot = lv_obj_create(o);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_center(dot);
    }
}

/* O danh dau: o vuong, nen amber khi da chon, dau tick bang Montserrat (Noto khong co glyph). */
static void check_mark(lv_obj_t *b, bool on)
{
    lv_obj_t *o = lv_obj_create(b);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 22, 22);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_border_width(o, on ? 0 : 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
    if (on) {
        lv_obj_t *l = lv_label_create(o);
        lv_obj_set_style_text_font(l, &fd_font_mont_24, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text(l, LV_SYMBOL_OK);
        lv_obj_center(l);
    }
}

static lv_obj_t *row_label(lv_obj_t *b, const char *txt)
{
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, txt ? txt : "");
    return l;
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_hor(body, 34, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_scrollable(body, false);

    ui_screen_title(body, _("DECK"));

    fd_sync_pick_t p;
    fd_sync_pick_get(&p);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = row_btn(body);
        radio_mark(b, (int)p.mode == i);
        row_label(b, _(MD_KEYS[i]));
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int nc = fd_sync_col_count();
    for (int i = 0; i < nc; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c)) continue;
        lv_obj_t *b = row_btn(list);
        check_mark(b, fd_sync_pick_col_on(i));
        row_label(b, c.name[0] ? c.name : c.id);
        lv_obj_add_event_cb(b, col_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

const ui_screen_t g_scr_deck = {
    .name = "deck", .parent = FD_SCR_HOME, .build = build,
};
