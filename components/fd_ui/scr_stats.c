/* 2.3.0 Man "Thong ke": LUON lay tu server (POST /api/study/stats, gui outbox truoc). Khong Wi-Fi -> bao can ket noi.
 * 2.3.4: noi dung trong vung cuon (s_list), nut Tro lai / Thu lai ghim day man; nhan thong ke can trai. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "ui_priv.h"

static lv_obj_t *s_body;
static lv_obj_t *s_list;   /* vung noi dung cuon (tren thanh nut) */
static bool s_gate, s_shown, s_left;

static void wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }
static void load(void);
static void retry_async(void *a) { (void)a; if (s_body) load(); }
static void retry_cb(lv_event_t *e) { (void)e; lv_async_call(retry_async, NULL); }   /* khong xoa nut dang xu ly su kien */

static lv_obj_t *text(const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(s_list ? s_list : s_body);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, s_left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, t);
    return l;
}

static lv_obj_t *btn_row(void)
{
    lv_obj_t *r = lv_obj_create(s_body);   /* con cuoi cua s_body, sau s_list (grow) -> ghim day */
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 44);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 8, 0);
    return r;
}

static void kv(const char *k, const char *v)
{
    lv_obj_t *r = lv_obj_create(s_list ? s_list : s_body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    /* 4.3.7: hang phang giong man Dong bo, khong the trang. */
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *a = lv_label_create(r);
    lv_obj_set_style_text_color(a, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(a, k);
    lv_obj_t *b = lv_label_create(r);
    lv_obj_set_style_text_color(b, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(b, v);
}

static void reset(lv_flex_align_t main)
{
    lv_obj_clean(s_body);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_scrollable(s_body, false);
    s_left = false;
    s_list = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
}

static void show_no_wifi(void)
{
    reset(LV_FLEX_ALIGN_CENTER);
    text(_("WIFI_NOT_CONNECTED"), &fd_font_noto_18, FD_COLOR_TEXT);
    text(_("STATS_NEED_WIFI"), NULL, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = btn_row();
    ui_gate_btn(r, _("OPEN_WIFI"), true, wifi_cb);
}

static void load(void)
{
    if (!fd_net_is_connected()) { show_no_wifi(); s_shown = true; return; }
    reset(LV_FLEX_ALIGN_CENTER);
    text(_("STATS_LOADING"), &fd_font_noto_18, FD_COLOR_TEXT_MUTED);
    s_shown = false;
    fd_sync_stats_fetch();
}

static void show(const fd_sync_stats_t *st)
{
    s_shown = true;
    if (st->state == FD_STATS_ERROR) {
        if (!strcmp(st->err, "no_wifi")) { show_no_wifi(); return; }
        if (!strcmp(st->err, "unpaired")) { lv_obj_clean(s_body); s_list = NULL; ui_pair_gate(s_body); return; }
        reset(LV_FLEX_ALIGN_CENTER);
        char m[64];
        snprintf(m, sizeof(m), _("ERROR_FMT"), st->err);
        text(m, &fd_font_noto_18, 0xDC2626);
        lv_obj_t *r = btn_row();
        ui_gate_btn(r, _("RETRY"), true, retry_cb);
        return;
    }
    reset(LV_FLEX_ALIGN_START);
    s_left = true;
    text(_("STATISTICS"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    char v[40];
    snprintf(v, sizeof(v), "%d", st->words); kv(_("STATS_WORDS"), v);
    snprintf(v, sizeof(v), "%d", st->mastered); kv(_("STATS_MASTERED"), v);
    snprintf(v, sizeof(v), "%d", st->learning); kv(_("STATS_LEARNING"), v);
    snprintf(v, sizeof(v), "%d", st->n_new); kv(_("STATS_NEW"), v);
    snprintf(v, sizeof(v), "%d", st->sentences); kv(_("SYNC_SENTENCES"), v);
    snprintf(v, sizeof(v), "%d", st->collections); kv(_("SYNC_COLLECTIONS"), v);
    snprintf(v, sizeof(v), _("STATS_ANS_FMT"), st->today_correct, st->today_answers); kv(_("STATS_TODAY"), v);
    snprintf(v, sizeof(v), _("STATS_ANS_FMT"), st->week_correct, st->week_answers); kv(_("STATS_WEEK"), v);
    snprintf(v, sizeof(v), _("STATS_DAYS_FMT"), st->streak_days); kv(_("STATS_STREAK"), v);
    snprintf(v, sizeof(v), "%d", st->scan_uses); kv(_("STATS_SCAN_USES"), v);
    snprintf(v, sizeof(v), "%d", st->scan_words); kv(_("STATS_SCAN_WORDS"), v);
    lv_obj_t *r = btn_row();
    ui_gate_btn(r, _("RETRY"), true, retry_cb);
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    load();
}

static void destroy(void) { s_body = s_list = NULL; }

static void on_tick(void)
{
    if (s_gate || !s_body || s_shown) return;
    fd_sync_stats_t st;
    fd_sync_stats_get(&st);
    if (st.state == FD_STATS_OK || st.state == FD_STATS_ERROR) show(&st);
}

const ui_screen_t g_scr_stats = {.name = "stats", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = on_tick};
