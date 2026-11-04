/* 2.1.6 Sap xep thanh cau (fd_sentence): the tu da xao, chon dung thu tu -> kiem tra -> phan hoi -> cau tiep.
 * Nut cung (theo o COLOUR_CELL cua xoay hien tai): TL = the truoc, TR = the sau, BL = xoa the vua chon, BR = chon.
 * Cham truc tiep vao the cung duoc. Chon du the -> tu kiem tra. 401/410/429/message: ui_nav + fd_ota xu ly chung. */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "fd_fonts.h"
#include "fd_sentence.h"
#include "ui_priv.h"

#define C_OK  0x16A34A
#define C_BAD 0xDC2626
#define COUNT 10

typedef enum { V_LOAD, V_MSG, V_PLAY, V_FEED, V_RESULT } view_t;

static view_t s_view;
static lv_obj_t *s_body;
static lv_obj_t *s_ans;             /* hang cau dang ghep */
static lv_obj_t *s_tile[FD_SEN_TOK];
static fd_sen_ex_t s_ex;
static int s_pos, s_total, s_correct, s_streak, s_best;
static int s_pick[FD_SEN_TOK], s_npick, s_cur;
static bool s_used[FD_SEN_TOK];
static int64_t s_t0, s_t_all;
static bool s_waiting;              /* dang cho /start */

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, t);
    return l;
}

static void body_reset(void)
{
    lv_obj_clean(s_body);
    s_ans = NULL;
    memset(s_tile, 0, sizeof(s_tile));
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 6, 0);
}

static lv_obj_t *button(lv_obj_t *p, const char *t, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *row(lv_obj_t *p, bool wrap)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_style_pad_row(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

static void show_play(void);
static void start(void);

/* ---------- LOAD / MSG ---------- */
static void retry_cb(lv_event_t *e) { (void)e; start(); }
static void home_cb(lv_event_t *e) { (void)e; ui_nav_home(); }
static void pair_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_PAIR); }
static void wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }

static void show_msg(const fd_sen_status_t *st)
{
    s_view = V_MSG;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *t, *sub = "", *bt = _("RETRY");
    lv_event_cb_t cb = retry_cb;
    char tmp[64];
    switch (st->err) {
    case FD_STUDY_ERR_UNPAIRED:
        t = _("PAIR_TO_STUDY"); sub = _("PAIR_HINT"); bt = _("PAIR_DEVICE"); cb = pair_cb;
        break;
    case FD_STUDY_ERR_NO_WIFI:
        t = _("WIFI_NOT_CONNECTED"); sub = _("STUDY_NEEDS_WIFI"); bt = _("OPEN_WIFI"); cb = wifi_cb;
        break;
    case FD_STUDY_ERR_NET:
        t = _("SERVER_UNREACHABLE"); sub = _("CHECK_WIFI_RETRY");
        break;
    case FD_STUDY_ERR_FORMAT:
        t = _("INVALID_QUESTION_DATA");
        break;
    default:
        snprintf(tmp, sizeof(tmp), _("SERVER_ERROR_CODE"), st->http);
        t = tmp;
        break;
    }
    label(s_body, t, &fd_font_noto_18, st->err == FD_STUDY_ERR_UNPAIRED ? FD_COLOR_TEXT : C_BAD);
    if (sub[0]) label(s_body, sub, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = row(s_body, false);
    button(r, _("BACK"), home_cb);
    button(r, bt, cb);
}

static void start(void)
{
    s_view = V_LOAD;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sp = lv_spinner_create(s_body);
    lv_obj_set_size(sp, 40, 40);
    label(s_body, _("SEN_LOADING"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    s_pos = s_correct = s_streak = s_best = 0;
    fd_sen_start(COUNT);   /* false = dang ban: van cho lan tai hien tai */
    s_waiting = true;
    s_t_all = esp_timer_get_time();
}

static void poll_load(void)
{
    if (s_view != V_LOAD || !s_waiting) return;
    fd_sen_status_t st;
    fd_sen_get(&st);
    if (st.busy) return;
    s_waiting = false;
    if (st.err || st.n <= 0) { show_msg(&st); return; }
    s_total = st.n;
    show_play();
}

/* ---------- PLAY ---------- */
static void ans_text(char *out, size_t cap)
{
    size_t o = 0;
    out[0] = 0;
    for (int k = 0; k < s_npick && o + 1 < cap; k++)
        o += snprintf(out + o, cap - o, "%s%s", k ? " " : "", s_ex.tok[s_pick[k]]);
    if (s_npick == 0) snprintf(out, cap, "%s", "…");
}

static void tile_style(int i)
{
    lv_obj_t *b = s_tile[i];
    if (!b) return;
    lv_obj_set_hidden(b, s_used[i]);
    bool cur = i == s_cur;
    lv_obj_set_style_border_width(b, cur ? 3 : 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(cur ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(cur ? FD_COLOR_PRIMARY_TINT : 0xFFFFFF), 0);
}

static void next_free(int dir)
{
    for (int k = 0; k < s_ex.n; k++) {
        s_cur = (s_cur + dir + s_ex.n) % s_ex.n;
        if (!s_used[s_cur]) return;
    }
}

static void refresh_play(void)
{
    char buf[FD_SEN_TXT_LEN + 8];
    ans_text(buf, sizeof(buf));
    if (s_ans) lv_label_set_text(s_ans, buf);
    for (int i = 0; i < s_ex.n; i++) tile_style(i);
}

static void show_feed(bool ok);

static void pick(int i)
{
    if (s_view != V_PLAY || i < 0 || i >= s_ex.n || s_used[i]) return;
    s_used[i] = true;
    s_pick[s_npick++] = i;
    if (s_npick >= s_ex.n) {
        uint32_t ms = (uint32_t)((esp_timer_get_time() - s_t0) / 1000);
        show_feed(fd_sen_answer(s_pos, s_pick, s_npick, ms));
        return;
    }
    if (s_used[s_cur]) next_free(1);
    refresh_play();
}

static void undo(void)
{
    if (s_view != V_PLAY || s_npick == 0) return;
    s_cur = s_pick[--s_npick];
    s_used[s_cur] = false;
    refresh_play();
}

static void tile_cb(lv_event_t *e) { pick((int)(intptr_t)lv_event_get_user_data(e)); }
static void undo_cb(lv_event_t *e) { (void)e; undo(); }

/* chu giai phim: o (0 TL..3 BR) -> mau phim theo xoay */
static void legend(lv_obj_t *p)
{
    static const char *const K[4] = {"SEN_PREV", "SEN_NEXT", "SEN_UNDO", "SEN_PICK"};
    lv_obj_t *r = row(p, true);
    for (int c = 0; c < 4; c++) {
        lv_obj_t *l = lv_label_create(r);
        lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
        lv_obj_set_style_border_width(l, 2, 0);
        lv_obj_set_style_border_color(l, lv_color_hex(ui_key_rgb(ui_cell_colour(c))), 0);
        lv_obj_set_style_radius(l, 6, 0);
        lv_obj_set_style_pad_hor(l, 6, 0);
        lv_label_set_text(l, _(K[c]));
    }
}

static void show_play(void)
{
    if (!fd_sen_ex(s_pos, &s_ex)) { fd_sen_status_t st; fd_sen_get(&st); st.err = FD_STUDY_ERR_FORMAT; show_msg(&st); return; }
    s_view = V_PLAY;
    s_npick = s_cur = 0;
    memset(s_used, 0, sizeof(s_used));
    body_reset();
    lv_obj_set_scrollable(s_body, true);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), _("SEN_PROGRESS"), s_pos + 1, s_total);
    label(s_body, tmp, &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    if (s_ex.hint[0]) label(s_body, s_ex.hint, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    s_ans = label(s_body, "", &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_set_style_border_width(s_ans, 1, 0);
    lv_obj_set_style_border_side(s_ans, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_ans, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_bottom(s_ans, 4, 0);
    lv_obj_set_clickable(s_ans, true);
    lv_obj_add_event_cb(s_ans, undo_cb, LV_EVENT_CLICKED, NULL);   /* cham cau = xoa the cuoi */
    lv_obj_t *pool = row(s_body, true);
    for (int i = 0; i < s_ex.n; i++) {
        lv_obj_t *b = lv_button_create(pool);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_style_pad_ver(b, 4, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_18, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_text(l, s_ex.tok[i]);
        lv_obj_add_event_cb(b, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_tile[i] = b;
    }
    legend(s_body);
    refresh_play();
    s_t0 = esp_timer_get_time();
}

/* ---------- FEEDBACK / RESULT ---------- */
static void show_result(void);
static void next(void)
{
    if (++s_pos < s_total) show_play();
    else show_result();
}
static void next_cb(lv_event_t *e) { (void)e; next(); }

static void show_feed(bool ok)
{
    s_view = V_FEED;
    if (ok) { s_correct++; if (++s_streak > s_best) s_best = s_streak; }
    else s_streak = 0;
    char said[FD_SEN_TXT_LEN + 8];
    ans_text(said, sizeof(said));
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(s_body, ok ? _("SEN_CORRECT") : _("SEN_WRONG"), &fd_font_noto_18, ok ? C_OK : C_BAD);
    if (!ok) label(s_body, said, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(s_body, _("SEN_ANSWER_IS"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(s_body, s_ex.sentence, &fd_font_noto_18, FD_COLOR_TEXT);
    if (s_ex.hint[0]) label(s_body, s_ex.hint, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = row(s_body, false);
    button(r, _("SEN_CONTINUE"), next_cb);
}

static void show_result(void)
{
    s_view = V_RESULT;
    fd_sen_submit(s_best, (uint32_t)((esp_timer_get_time() - s_t_all) / 1000));
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), _("SEN_RESULT"), s_correct, s_total);
    label(s_body, tmp, &fd_font_noto_18, s_correct * 2 >= s_total ? C_OK : C_BAD);
    lv_obj_t *r = row(s_body, false);
    button(r, _("BACK"), home_cb);
    button(r, _("SEN_AGAIN"), retry_cb);
}

/* ---------- screen ---------- */
static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    start();
}

static void destroy(void) { s_body = s_ans = NULL; memset(s_tile, 0, sizeof(s_tile)); }

static void on_tick(void) { poll_load(); }

static bool on_button(const fd_evt_button_t *b)
{
    if (b->action != FD_BTN_CLICK) return false;
    if (s_view == V_PLAY) {
        switch (ui_key_cell(b->index)) {
        case 0: next_free(-1); refresh_play(); break;
        case 1: next_free(1); refresh_play(); break;
        case 2: undo(); break;
        case 3: pick(s_cur); break;
        default: break;
        }
        return true;
    }
    if (s_view == V_FEED) { next(); return true; }
    if (s_view == V_RESULT) { start(); return true; }
    return false;
}

const ui_screen_t g_scr_sentence = {
    .name = "sentence", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = on_tick,
    .on_button = on_button,
};
