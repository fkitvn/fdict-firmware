/* 2.3.0 Man "Thong ke": LUON lay tu server (POST /api/study/stats, gui outbox truoc). Khong Wi-Fi -> bao can ket noi.
 * 2.3.4: noi dung trong vung cuon (s_list). 4.5.4: bo nut Thu lai, them tieu de ghim tren cung (trai, giong module khac).
 * 4.5.4: ba the Tu, Cau, Hoat dong 7 ngay (cot 7 ngay, phien, ti le dung, chuoi ngay, theo module).
 * Truong thieu (may chu cu khong gui days) hien 0. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "ui_priv.h"

static lv_obj_t *s_body;
static lv_obj_t *s_list;   /* vung noi dung cuon (tren thanh nut) */
static bool s_gate, s_shown;

static void wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }

static lv_obj_t *text_on(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = ui_label_create(p, t, f ? f : &fd_font_noto_14, c);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t *btn_row(void)
{
    lv_obj_t *r = lv_obj_create(s_body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 44);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 8, 0);
    return r;
}

static void reset(lv_flex_align_t main)
{
    lv_obj_clean(s_body);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    lv_obj_set_scrollable(s_body, false);
    /* Tieu de giong cac module (noto 18, mau chinh, 26 px), can trai, khong cuon theo noi dung. */
    lv_obj_t *t = ui_label_create(s_body, _("STATISTICS"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_height(t, 26);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    s_list = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
}

static void show_no_wifi(void)
{
    reset(LV_FLEX_ALIGN_CENTER);
    text_on(s_list, _("WIFI_NOT_CONNECTED"), &fd_font_noto_18, FD_COLOR_TEXT);
    text_on(s_list, _("STATS_NEED_WIFI"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = btn_row();
    ui_gate_btn(r, _("OPEN_WIFI"), true, wifi_cb);
}

static void load(void)
{
    if (!fd_net_is_connected()) { show_no_wifi(); s_shown = true; return; }
    reset(LV_FLEX_ALIGN_CENTER);
    text_on(s_list, _("STATS_LOADING"), &fd_font_noto_18, FD_COLOR_TEXT_MUTED);
    s_shown = false;
    fd_sync_stats_fetch();
}

static lv_obj_t *card(void)
{
    lv_obj_t *c = lv_obj_create(s_list);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_set_style_pad_row(c, 6, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_font(c, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(c, lv_color_hex(FD_COLOR_TEXT), 0);
    return c;
}

static void card_title(lv_obj_t *c, const char *t)
{
    lv_obj_t *l = ui_label_create(c, t, &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_width(l, lv_pct(100));
}

static void muted(lv_obj_t *p, const char *t)
{
    lv_obj_t *l = ui_label_create(p, t, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
}

/* Thanh tien do rong 100%, fill = done/total (total 0 -> 0). */
static void bar(lv_obj_t *p, int done, int total)
{
    if (done < 0) done = 0;
    if (total < 0) total = 0;
    int pct = (total > 0) ? (int)(((int32_t)done * 100) / total) : 0;
    if (pct > 100) pct = 100;
    lv_obj_t *track = lv_obj_create(p);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, lv_pct(100), 8);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(track, true, 0);
    if (pct <= 0) return;
    lv_obj_t *fill = lv_obj_create(track);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, lv_pct(pct), 8);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
}

static void progress_row(lv_obj_t *p, const char *name, int done, int total)
{
    char v[24];
    if (done < 0) done = 0;
    if (total < 0) total = 0;
    snprintf(v, sizeof(v), "%d/%d", done, total);
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(r, false);
    lv_obj_t *a = ui_label_create(r, name, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(a, 1);
    ui_label_create(r, v, &fd_font_noto_14, FD_COLOR_TEXT);
    bar(p, done, total);
}

static void kv_row(lv_obj_t *p, const char *k, const char *v)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(r, false);
    lv_obj_t *a = ui_label_create(r, k, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(a, 1);
    ui_label_create(r, v, &fd_font_noto_14, FD_COLOR_TEXT);
}

static int ymd_dow(const char *ymd)
{
    int y, m, d;
    if (!ymd || sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3) return -1;
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char *dow_lab(int dow)
{
    static char buf[8];
    const char *s = _("STATS_DOW");
    buf[0] = 0;
    if (dow < 0 || dow > 6 || !s) return buf;
    for (int i = 0; i < dow; i++) {
        const char *c = strchr(s, ',');
        if (!c) return buf;
        s = c + 1;
    }
    size_t n = 0;
    while (s[n] && s[n] != ',' && n + 1 < sizeof(buf)) n++;
    memcpy(buf, s, n);
    buf[n] = 0;
    return buf;
}

/* Co it nhat 1 ngay co hoat dong (may chu cu khong gui days -> tat ca 0 -> an bieu do). */
static bool has_day_data(const fd_sync_stats_t *st)
{
    for (int i = 0; i < FD_STATS_DAYS; i++)
        if (st->day[i].answers > 0) return true;
    return false;
}

static void day_chart(lv_obj_t *p, const fd_sync_stats_t *st)
{
    int max_a = 1;
    for (int i = 0; i < FD_STATS_DAYS; i++)
        if (st->day[i].answers > max_a) max_a = st->day[i].answers;
    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_scrollable(row, false);
    const int track_h = 48;
    for (int i = 0; i < FD_STATS_DAYS; i++) {
        int a = st->day[i].answers;
        if (a < 0) a = 0;
        int h = (a * track_h) / max_a;
        if (a > 0 && h < 3) h = 3;
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_remove_style_all(col);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 3, 0);
        lv_obj_set_scrollable(col, false);
        lv_obj_t *track = lv_obj_create(col);
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, 14, track_h);
        lv_obj_set_style_radius(track, 4, 0);
        lv_obj_set_style_bg_color(track, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        if (h > 0) {
            lv_obj_t *fill = lv_obj_create(track);
            lv_obj_remove_style_all(fill);
            lv_obj_set_size(fill, 14, h);
            lv_obj_set_style_radius(fill, 4, 0);
            lv_obj_set_style_bg_color(fill, lv_color_hex(i == FD_STATS_DAYS - 1 ? FD_COLOR_PRIMARY_DARK : FD_COLOR_PRIMARY), 0);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
        int dow = ymd_dow(st->day[i].date);
        const char *lab = dow_lab(dow);
        if (!lab[0]) lab = "·";
        lv_obj_t *tl = ui_label_create(col, lab, &fd_font_noto_14, i == FD_STATS_DAYS - 1 ? FD_COLOR_PRIMARY_TEXT : FD_COLOR_TEXT_MUTED);
        lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static int pct_of(int correct, int answers, int hinted)
{
    int scored = answers - hinted;
    if (scored <= 0) return 0;
    if (correct < 0) correct = 0;
    int p = (int)(((int32_t)correct * 100) / scored);
    return p > 100 ? 100 : p;
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
        text_on(s_list, m, &fd_font_noto_18, 0xDC2626);
        return;
    }
    reset(LV_FLEX_ALIGN_START);

    /* ----- Tu ----- */
    lv_obj_t *wcard = card();
    card_title(wcard, _("STATS_VOCAB"));
    char v[64], sub[80];
    snprintf(v, sizeof(v), "%d/%d", st->mastered, st->words);
    lv_obj_t *big = ui_label_create(wcard, v, &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_set_width(big, lv_pct(100));
    muted(wcard, _("STATS_MASTERED"));
    bar(wcard, st->mastered, st->words);
    snprintf(sub, sizeof(sub), "%s %d · %s %d", _("STATS_LEARNING"), st->learning, _("STATS_NEW"), st->n_new);
    muted(wcard, sub);
    progress_row(wcard, _("PRACTICE"), st->word_quiz, st->words);
    progress_row(wcard, _("WORD_TYPE"), st->word_pos, st->words);
    progress_row(wcard, _("SPELLING"), st->word_spell, st->words);
    snprintf(v, sizeof(v), "%d", st->collections);
    kv_row(wcard, _("SYNC_COLLECTIONS"), v);

    /* ----- Cau ----- */
    lv_obj_t *scard = card();
    card_title(scard, _("STATS_SEN_SECTION"));
    snprintf(v, sizeof(v), "%d/%d", st->sen_mastered, st->sentences);
    big = ui_label_create(scard, v, &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_set_width(big, lv_pct(100));
    muted(scard, _("STATS_SEN_DONE"));
    bar(scard, st->sen_mastered, st->sentences);
    progress_row(scard, _("FILL_BLANK"), st->sen_cloze, st->sentences);
    progress_row(scard, _("SENTENCE"), st->sen_order, st->sentences);
    progress_row(scard, _("LISTEN"), st->sen_hear, st->sentences);

    /* ----- Hoat dong 7 ngay ----- */
    lv_obj_t *acard = card();
    card_title(acard, _("STATS_ACTIVITY"));
    if (has_day_data(st)) day_chart(acard, st);
    int scored = st->week_answers - st->week_hinted;
    if (scored < 0) scored = 0;
    snprintf(v, sizeof(v), _("STATS_ANS_FMT"), st->week_correct, scored);
    kv_row(acard, _("STATS_WEEK"), v);
    snprintf(v, sizeof(v), "%d", st->week_sessions);
    kv_row(acard, _("STATS_SESSIONS"), v);
    snprintf(v, sizeof(v), "%d%%", pct_of(st->week_correct, st->week_answers, st->week_hinted));
    kv_row(acard, _("STATS_ACCURACY"), v);
    snprintf(v, sizeof(v), _("STATS_DAYS_FMT"), st->streak_days);
    kv_row(acard, _("STATS_STREAK"), v);
    scored = st->today_answers - st->today_hinted;
    if (scored < 0) scored = 0;
    snprintf(v, sizeof(v), _("STATS_ANS_FMT"), st->today_correct, scored);
    kv_row(acard, _("STATS_TODAY"), v);
    static const char *const MOD_KEY[FD_MOD_N] = {
        "PRACTICE", "WORD_TYPE", "SPELLING", "FILL_BLANK", "SENTENCE", "LISTEN"
    };
    for (int i = 0; i < FD_MOD_N; i++) {
        int n = snprintf(v, sizeof(v), _("STATS_MOD_FMT"), st->mod[i].sessions, st->mod[i].correct,
                         st->mod[i].answers - st->mod[i].hinted);
        if (st->mod[i].hinted > 0 && n > 0 && n < (int)sizeof(v))
            snprintf(v + n, sizeof(v) - (size_t)n, _("STATS_MOD_HINT_FMT"), st->mod[i].hinted);
        kv_row(acard, _(MOD_KEY[i]), v);
    }
    snprintf(v, sizeof(v), "%d", st->scan_uses);
    kv_row(acard, _("STATS_SCAN_USES"), v);
    snprintf(v, sizeof(v), "%d", st->scan_words);
    kv_row(acard, _("STATS_SCAN_WORDS"), v);
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
