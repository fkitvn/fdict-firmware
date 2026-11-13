/* 4.3.7: man cau hinh bo tu dung chung 5 module hoc.
 * Hang 1: ba nut chon mot (radio) Tat ca, Tu moi them, Ket hop.
 * Hang 2: danh sach bo tu xep ngang, chi hien khi chon Ket hop.
 * Nut Luu lai o cuoi: chi ghi NVS khi bam Luu; quay lai khong luu. */
#include <string.h>
#include "fd_fonts.h"
#include "fd_sync.h"
#include "ui_priv.h"

static const char *const MD_KEYS[] = {"ALL", "NEW_ADDED", "COMBINED"};

static bool s_refreshing;
static bool s_draft;
static fd_sync_pick_mode_t s_mode;
static int s_nsel;
static char s_sel[FD_SYNC_MAX_COLS][FD_SYNC_WID_LEN];

static bool portrait(void)
{
    return lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL);
}

static bool draft_has(const char *id)
{
    if (!id || !id[0]) return false;
    for (int i = 0; i < s_nsel; i++)
        if (strcmp(s_sel[i], id) == 0) return true;
    return false;
}

static void draft_clear(void)
{
    s_draft = false;
    s_nsel = 0;
    s_mode = FD_SYNC_PICK_ALL;
    memset(s_sel, 0, sizeof(s_sel));
}

static void draft_select_all(void)
{
    s_nsel = 0;
    int nc = fd_sync_col_count();
    for (int i = 0; i < nc && s_nsel < FD_SYNC_MAX_COLS; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c) || !c.id[0]) continue;
        strncpy(s_sel[s_nsel], c.id, FD_SYNC_WID_LEN - 1);
        s_sel[s_nsel][FD_SYNC_WID_LEN - 1] = 0;
        s_nsel++;
    }
}

static void draft_load(void)
{
    if (s_draft) return;
    fd_sync_pick_t p;
    fd_sync_pick_get(&p);
    s_mode = p.mode;
    if (s_mode > FD_SYNC_PICK_COMBINED) s_mode = FD_SYNC_PICK_ALL;
    s_nsel = 0;
    memset(s_sel, 0, sizeof(s_sel));
    int nc = fd_sync_col_count();
    for (int i = 0; i < nc && s_nsel < FD_SYNC_MAX_COLS; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c) || !c.id[0]) continue;
        if (!fd_sync_pick_col_on(i)) continue;
        strncpy(s_sel[s_nsel], c.id, FD_SYNC_WID_LEN - 1);
        s_sel[s_nsel][FD_SYNC_WID_LEN - 1] = 0;
        s_nsel++;
    }
    s_draft = true;
}

static void draft_toggle(int col_idx)
{
    fd_sync_col_t c;
    if (!fd_sync_col_get(col_idx, &c) || !c.id[0]) return;
    int found = -1;
    for (int i = 0; i < s_nsel; i++) {
        if (strcmp(s_sel[i], c.id) == 0) { found = i; break; }
    }
    if (found >= 0) {
        for (int i = found; i < s_nsel - 1; i++)
            memcpy(s_sel[i], s_sel[i + 1], FD_SYNC_WID_LEN);
        s_nsel--;
        memset(s_sel[s_nsel], 0, FD_SYNC_WID_LEN);
    } else if (s_nsel < FD_SYNC_MAX_COLS) {
        strncpy(s_sel[s_nsel], c.id, FD_SYNC_WID_LEN - 1);
        s_sel[s_nsel][FD_SYNC_WID_LEN - 1] = 0;
        s_nsel++;
    }
    s_mode = FD_SYNC_PICK_COMBINED;
}

/* ui_nav_refresh chi dat lich ve lai; giu s_refreshing=true den khi build chay xong
 * de destroy khong xoa ban nhap (neu xoa, man ve lai tu NVS va lua chon vua bam bien mat). */
static void kick_refresh(void)
{
    s_refreshing = true;
    ui_nav_refresh();
}

static void mode_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v < 0 || v > (int)FD_SYNC_PICK_COMBINED) return;
    fd_sync_pick_mode_t prev = s_mode;
    s_mode = (fd_sync_pick_mode_t)v;
    if (s_mode == FD_SYNC_PICK_COMBINED && prev != FD_SYNC_PICK_COMBINED && s_nsel == 0)
        draft_select_all();
    kick_refresh();
}

static void col_cb(lv_event_t *e)
{
    draft_toggle((int)(intptr_t)lv_event_get_user_data(e));
    kick_refresh();
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    fd_sync_pick_set(s_mode, s_sel, s_nsel);
    draft_clear();
    ui_nav_back();
}


/* Nut chon mot: vong tron, cham giua khi dang chon. */
static void radio_dot(lv_obj_t *b, bool on)
{
    lv_obj_t *o = lv_obj_create(b);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 16, 16);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(on ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    if (on) {
        lv_obj_t *dot = lv_obj_create(o);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(FD_COLOR_PRIMARY), 0);
        lv_obj_center(dot);
    }
}

/* O danh dau: o vuong, nen amber khi da chon, dau tick bang Montserrat (Noto khong co glyph). */
static void check_mark(lv_obj_t *b, bool on)
{
    lv_obj_t *o = lv_obj_create(b);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 18, 18);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_border_width(o, on ? 0 : 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, on ? lv_color_white() : lv_color_white(), 0);
    if (on) {
        lv_obj_set_style_bg_color(o, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_30, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_t *l = lv_label_create(o);
        lv_obj_set_style_text_font(l, &fd_font_mont_24, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text(l, LV_SYMBOL_OK);
        lv_obj_center(l);
    }
}

static void build(lv_obj_t *scr)
{
    draft_load();
    s_refreshing = false;

    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(body, 10, 0);
    lv_obj_set_style_pad_row(body, 8, 0);

    /* Nhan nho, can trai, cung kieu nhan cac hang cai dat khac. */
    lv_obj_t *ttl = lv_label_create(body);
    lv_obj_set_width(ttl, lv_pct(100));
    lv_obj_set_style_text_color(ttl, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(ttl, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(ttl, _("DECK_CONFIG"));

    bool por = portrait();
    int32_t bh = por ? 36 : 34;

    lv_obj_t *modes = lv_obj_create(body);
    lv_obj_remove_style_all(modes);
    lv_obj_set_width(modes, lv_pct(100));
    lv_obj_set_height(modes, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modes, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modes, 6, 0);
    lv_obj_set_scrollable(modes, false);
    lv_obj_set_clickable(modes, false);

    for (int i = 0; i < 3; i++) {
        bool on = (int)s_mode == i;
        lv_obj_t *b = lv_button_create(modes);
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, on ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(on ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
        lv_obj_set_style_pad_hor(b, 2, 0);
        lv_obj_set_style_pad_ver(b, 4, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 4, 0);
        radio_dot(b, on);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(l, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_text(l, _(MD_KEYS[i]));
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(l, -3, 0);
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *chips = lv_obj_create(body);
    lv_obj_remove_style_all(chips);
    lv_obj_set_width(chips, lv_pct(100));
    lv_obj_set_flex_grow(chips, 1);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(chips, 6, 0);
    lv_obj_set_style_pad_row(chips, 6, 0);
    lv_obj_set_style_pad_top(chips, 2, 0);
    lv_obj_set_scroll_dir(chips, LV_DIR_VER);
    lv_obj_set_clickable(chips, false);

    /* Danh sach bo tu chi hien khi chon Ket hop; khung van giu cho de nut Luu lai nam cuoi. */
    int nc = (s_mode == FD_SYNC_PICK_COMBINED) ? fd_sync_col_count() : 0;
    for (int i = 0; i < nc; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c)) continue;
        bool on = draft_has(c.id);
        lv_obj_t *b = lv_button_create(chips);
        lv_obj_set_height(b, bh);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
        lv_obj_set_style_border_width(b, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 6, 0);
        check_mark(b, on);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(l, on ? lv_color_white() : lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_text(l, c.name[0] ? c.name : c.id);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, 88);
        lv_obj_add_event_cb(b, col_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *save = lv_button_create(body);
    lv_obj_set_size(save, lv_pct(60), 36);
    lv_obj_set_style_radius(save, 18, 0);
    lv_obj_set_style_shadow_width(save, 0, 0);
    lv_obj_set_style_border_width(save, 0, 0);
    lv_obj_set_style_bg_color(save, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *sl = lv_label_create(save);
    lv_obj_set_style_text_font(sl, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_label_set_text(sl, _("SAVE"));
    lv_obj_center(sl);
    lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, NULL);
}

static void destroy(void)
{
    if (!s_refreshing) draft_clear();
}

static bool on_back(void)
{
    draft_clear();
    return false;
}

const ui_screen_t g_scr_deck = {
    .name = "deck", .parent = FD_SCR_HOME, .build = build,
    .destroy = destroy, .on_back = on_back,
};
