/* Man cau hinh bo tu dung chung 5 module hoc.
 * Ba che do: Tat ca, Tu moi them, Ket hop.
 * Ket hop: moi bo mot hang, ten dai cat bang dau cham. Cham mot bo chi doi mau hang do,
 * khong ve lai man, nen danh sach dai van dung cho. */
#include <stdio.h>
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
static lv_obj_t *s_count;
static lv_obj_t *s_list;

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
    s_count = NULL;
    s_list = NULL;
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

static void draft_select_none(void)
{
    s_nsel = 0;
    memset(s_sel, 0, sizeof(s_sel));
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

static void kick_refresh(void)
{
    s_refreshing = true;
    ui_nav_refresh();
}

static void refresh_count(void)
{
    if (!s_count) return;
    char buf[48];
    snprintf(buf, sizeof(buf), _("DECK_PICK_N"), s_nsel, fd_sync_col_count());
    lv_label_set_text(s_count, buf);
}

/* O vuong + dau tick. Tick luon co, de trong khi chua chon. */
static void paint_row(lv_obj_t *b, bool on)
{
    lv_obj_set_style_bg_color(b, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
    lv_obj_set_style_border_width(b, on ? 0 : 1, 0);
    lv_obj_t *box = lv_obj_get_child(b, 0);
    lv_obj_t *name = lv_obj_get_child(b, 1);
    if (box) {
        lv_obj_set_style_border_width(box, on ? 0 : 2, 0);
        lv_obj_set_style_bg_opa(box, on ? LV_OPA_30 : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(box, lv_color_white(), 0);
        lv_obj_t *tick = lv_obj_get_child(box, 0);
        if (tick) lv_label_set_text(tick, on ? LV_SYMBOL_OK : "");
    }
    if (name)
        lv_obj_set_style_text_color(name, on ? lv_color_white() : lv_color_hex(FD_COLOR_TEXT), 0);
}

static void paint_list(void)
{
    if (!s_list) return;
    uint32_t n = lv_obj_get_child_count(s_list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *b = lv_obj_get_child(s_list, i);
        int idx = (int)(intptr_t)lv_obj_get_user_data(b);
        fd_sync_col_t c;
        bool on = fd_sync_col_get(idx, &c) && draft_has(c.id);
        paint_row(b, on);
    }
    refresh_count();
}

static void mode_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v < 0 || v > (int)FD_SYNC_PICK_COMBINED) return;
    s_mode = (fd_sync_pick_mode_t)v;
    kick_refresh();
}

static void col_cb(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(b);
    draft_toggle(idx);
    fd_sync_col_t c;
    bool on = fd_sync_col_get(idx, &c) && draft_has(c.id);
    paint_row(b, on);
    refresh_count();
}

static void bulk_cb(lv_event_t *e)
{
    if ((int)(intptr_t)lv_event_get_user_data(e)) draft_select_all();
    else draft_select_none();
    s_mode = FD_SYNC_PICK_COMBINED;
    paint_list();
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    fd_sync_pick_set(s_mode, s_sel, s_nsel);
    draft_clear();
    ui_nav_back();
}

static void radio_dot(lv_obj_t *b, bool on)
{
    lv_obj_t *o = lv_obj_create(b);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 14, 14);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(on ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    if (on) {
        lv_obj_t *dot = lv_obj_create(o);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(FD_COLOR_PRIMARY), 0);
        lv_obj_center(dot);
    }
}

static lv_obj_t *text_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 28);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void add_row(lv_obj_t *list, int idx, const fd_sync_col_t *c)
{
    bool on = draft_has(c->id);
    int32_t name_w = lv_display_get_horizontal_resolution(NULL) - 72;
    if (name_w < 80) name_w = 80;

    lv_obj_t *b = lv_button_create(list);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, 34);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 8, 0);
    lv_obj_set_user_data(b, (void *)(intptr_t)idx);

    lv_obj_t *box = lv_obj_create(b);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 18, 18);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_t *tick = lv_label_create(box);
    lv_obj_set_style_text_font(tick, &fd_font_mont_24, 0);
    lv_obj_set_style_text_color(tick, lv_color_white(), 0);
    lv_obj_center(tick);

    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_width(l, name_w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, c->name[0] ? c->name : c->id);
    lv_obj_add_event_cb(b, col_cb, LV_EVENT_CLICKED, NULL);
    paint_row(b, on);
}

static void build(lv_obj_t *scr)
{
    draft_load();
    s_refreshing = false;
    s_count = NULL;
    s_list = NULL;

    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(body, 8, 0);
    lv_obj_set_style_pad_row(body, 6, 0);

    lv_obj_t *ttl = lv_label_create(body);
    lv_obj_set_width(ttl, lv_pct(100));
    lv_obj_set_style_text_color(ttl, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(ttl, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(ttl, _("DECK_CONFIG"));

    lv_obj_t *modes = lv_obj_create(body);
    lv_obj_remove_style_all(modes);
    lv_obj_set_width(modes, lv_pct(100));
    lv_obj_set_height(modes, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modes, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modes, 6, 0);
    lv_obj_set_scrollable(modes, false);

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
        lv_obj_set_style_pad_row(b, 2, 0);
        radio_dot(b, on);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(l, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_text(l, _(MD_KEYS[i]));
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    if (s_mode == FD_SYNC_PICK_COMBINED) {
        int nc = fd_sync_col_count();
        lv_obj_t *tools = lv_obj_create(body);
        lv_obj_remove_style_all(tools);
        lv_obj_set_width(tools, lv_pct(100));
        lv_obj_set_height(tools, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tools, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(tools, 6, 0);
        lv_obj_set_scrollable(tools, false);

        s_count = lv_label_create(tools);
        lv_obj_set_flex_grow(s_count, 1);
        lv_obj_set_style_text_font(s_count, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(s_count, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
        lv_label_set_long_mode(s_count, LV_LABEL_LONG_DOT);
        refresh_count();
        if (nc > 0) {
            text_btn(tools, _("DECK_PICK_ALL"), bulk_cb, (void *)(intptr_t)1);
            text_btn(tools, _("DECK_PICK_NONE"), bulk_cb, (void *)(intptr_t)0);
        }

        if (nc <= 0) {
            lv_obj_t *empty = lv_label_create(body);
            lv_obj_set_width(empty, lv_pct(100));
            lv_obj_set_style_text_font(empty, &fd_font_noto_14, 0);
            lv_obj_set_style_text_color(empty, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
            lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(empty, _("LOOKUP_NO_COLLECTION"));
        } else {
            s_list = lv_obj_create(body);
            lv_obj_remove_style_all(s_list);
            lv_obj_set_width(s_list, lv_pct(100));
            lv_obj_set_flex_grow(s_list, 1);
            lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(s_list, 4, 0);
            lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
            for (int i = 0; i < nc; i++) {
                fd_sync_col_t c;
                if (!fd_sync_col_get(i, &c)) continue;
                add_row(s_list, i, &c);
            }
        }
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
    else {
        s_count = NULL;
        s_list = NULL;
    }
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
