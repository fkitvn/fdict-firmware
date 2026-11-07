/* 2.1.6 Sap xep thanh cau (fd_sentence): the tu da xao, chon dung thu tu -> kiem tra -> phan hoi -> cau tiep.
 * Nut cung (theo o COLOUR_CELL cua xoay hien tai): TL = the truoc, TR = the sau, BL = xoa the vua chon, BR = chon.
 * Moi tu la 1 the chu nhat (vien). Cham the de chon lan luot -> the chuyen len vung cau; cham vung cau = xoa tu cuoi.
 * Chon du the -> tu kiem tra ngay (cham tai may). 2.1.7: hoc offline (cache / bo cau mau), ket qua dong bo sau.
 * 2.2.0: man cai dat (phat am sau moi cau, NVS fd/sen_snd) truoc khi Bat dau.
 * 2.3.4: them so cau/phien (5|10, NVS fd/sen_cnt, mac dinh 10 - server chi nhan 5|10) va huong dich
 *   (NVS fd/sen_dir, mac dinh 0 = Viet>Anh: goi y tieng Viet, xep the tieng Anh nhu cu; 1 = Anh>Viet: hien
 *   cau tieng Anh, xep the tieng Viet tach tu nghia/hint tai may; hint khong tach duoc -> cau do xep tieng Anh).
 *   Phat am luon doc cau tieng Anh. Nut hanh dong ghim day man (noi dung cuon o tren); bo nhan SEN_TAP_HINT. */
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sentence.h"
#include "fd_voice.h"
#include "nvs.h"
#include "ui_priv.h"

#define C_OK  0x16A34A
#define C_BAD 0xDC2626
#define SEN_REP_KEY "sen_rep"   /* 2.3.6 NVS fd: tu dong phat cau dung 0 Tat (mac dinh) / 1 / 2 lan (thay sen_snd) */
#define SEN_CNT_KEY "sen_cnt"   /* NVS fd: so cau/phien 5|10 */
#define SEN_DIR_KEY "sen_dir"   /* NVS fd: 0 = Viet>Anh, 1 = Anh>Viet */

typedef enum { V_SETUP, V_LOAD, V_MSG, V_PLAY, V_FEED, V_RESULT } view_t;

static view_t s_view;
static lv_obj_t *s_body;
static lv_obj_t *s_ans;             /* vung cau dang ghep (hang the da chon) */
static lv_obj_t *s_tile[FD_SEN_TOK];
static fd_sen_ex_t s_ex;
static int s_pos, s_total, s_correct, s_streak, s_best;
static int s_pick[FD_SEN_TOK], s_npick, s_cur;
static bool s_used[FD_SEN_TOK];
static int64_t s_t0, s_t_all;
static bool s_waiting;              /* dang cho /start */
static uint8_t s_reps;               /* 2.3.6: tu dong phat cau dung sau moi cau: 0 Tat / 1 / 2 lan (NVS) */
static int s_cnt = 10;              /* so cau/phien (NVS) */
static bool s_vi;                   /* huong Anh>Viet (NVS) */
static bool s_loc;                  /* cau hien tai: the tieng Viet tach tai may */
static int s_vn;
static char s_vt[FD_SEN_TOK][FD_SEN_TOK_LEN];    /* the tieng Viet da xao */
static char s_vans[FD_SEN_TOK][FD_SEN_TOK_LEN];  /* thu tu dung */
static lv_obj_t *s_cnt_lbl, *s_dir_lbl;

static void prefs_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0, c = 10, d = 0;   /* 2.3.6 mac dinh: tu dong phat TAT, 10 cau, Viet>Anh */
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, SEN_REP_KEY, &v);
        nvs_get_u8(h, SEN_CNT_KEY, &c);
        nvs_get_u8(h, SEN_DIR_KEY, &d);
        nvs_close(h);
    }
    s_reps = v <= 2 ? v : 0;
    s_cnt = c == 5 ? 5 : 10;
    s_vi = d == 1;
}

static void pref_save(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

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
    s_cnt_lbl = s_dir_lbl = NULL;
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

static void tile_box(lv_obj_t *b, bool cur)
{
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, cur ? 3 : 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(cur ? FD_COLOR_PRIMARY : 0x9CA3AF), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(cur ? FD_COLOR_PRIMARY_TINT : 0xFFFFFF), 0);
}

static lv_obj_t *tile_label(lv_obj_t *b, const char *t)
{
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(l, t);
    return l;
}

/* vung noi dung cuon (gian) - nut hanh dong them vao s_body sau no -> ghim day */
static lv_obj_t *content(void)
{
    lv_obj_t *c = lv_obj_create(s_body);
    lv_obj_remove_style_all(c);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    return c;
}

static void show_play(void);
static void show_setup(void);
static void start(void);

/* ---------- LOAD / MSG ---------- */
static void retry_cb(lv_event_t *e) { (void)e; show_setup(); }
static void home_cb(lv_event_t *e) { (void)e; ui_nav_home(); }
static void pair_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_PAIR); }
static void wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }
static void sync_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_SYNC); }
static void setup_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_setup(); }
enum { OPT_CNT, OPT_DIR, OPT_REP };
static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_CNT) { s_cnt = v ? 10 : 5; pref_save(SEN_CNT_KEY, (uint8_t)s_cnt); }
    else if (grp == OPT_DIR) { s_vi = v == 1; pref_save(SEN_DIR_KEY, s_vi ? 1 : 0); }
    else if (grp == OPT_REP) { s_reps = (uint8_t)v; pref_save(SEN_REP_KEY, s_reps); }
    lv_async_call(setup_async, NULL);   /* dung lai man ngoai callback nut */
}
static void begin_cb(lv_event_t *e) { (void)e; start(); }

static void show_setup(void)
{
    s_view = V_SETUP;
    body_reset();
    lv_obj_t *sc = ui_setup_begin(s_body, _("SENTENCE"));   /* 2.3.6: mau cai dat chung */
    static const char *const CNT[] = {"5", "10"};
    static const char *const DIR[] = {"SEN_DIR_VI_EN", "SEN_DIR_EN_VI"};
    static const char *const RP[] = {"OFF", "REPEAT_1", "REPEAT_2"};
    ui_setup_opt_row(sc, _("SEN_COUNT"), CNT, 2, s_cnt == 10 ? 1 : 0, opt_cb, OPT_CNT, UI_OPT_WIDE);
    ui_setup_opt_row(sc, _("SEN_DIR"), DIR, 2, s_vi ? 1 : 0, opt_cb, OPT_DIR, UI_OPT_WIDE);
    ui_setup_opt_row(sc, _("AUTO_PLAY"), RP, 3, s_reps, opt_cb, OPT_REP, UI_OPT_WIDE);
    ui_setup_start(s_body, begin_cb);
}

static void show_msg(const fd_sen_status_t *st)
{
    s_view = V_MSG;
    body_reset();
    lv_obj_t *c = content();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *t, *sub = "", *bt = _("RETRY");
    lv_event_cb_t cb = retry_cb;
    char tmp[64];
    switch (st->err) {
    case FD_STUDY_ERR_UNPAIRED:
        t = _("PAIR_TO_STUDY"); sub = _("PAIR_HINT"); bt = _("PAIR_STEP_ID"); cb = pair_cb;
        break;
    case FD_STUDY_ERR_NO_WIFI:
        t = _("WIFI_NOT_CONNECTED"); sub = _("STUDY_NEEDS_WIFI"); bt = _("OPEN_WIFI"); cb = wifi_cb;
        break;
    case FD_STUDY_ERR_NET:
        t = _("SERVER_UNREACHABLE"); sub = _("CHECK_WIFI_RETRY");
        break;
    case FD_STUDY_ERR_NO_DECK:
        t = _("NO_OFFLINE_DATA_YET");
        sub = _("SYNC_AUTO_HINT");
        bt = _("SYNC_NOW");
        cb = sync_cb;
        break;
    case FD_STUDY_ERR_FORMAT:
        t = _("INVALID_QUESTION_DATA");
        break;
    default:
        snprintf(tmp, sizeof(tmp), _("SERVER_ERROR_CODE"), st->http);
        t = tmp;
        break;
    }
    label(c, t, &fd_font_noto_18, st->err == FD_STUDY_ERR_UNPAIRED ? FD_COLOR_TEXT : C_BAD);
    if (sub[0]) label(c, sub, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
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
    fd_sen_start(s_cnt);   /* false = dang ban: van cho lan tai hien tai */
    s_waiting = true;
    fd_sen_sync();
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
#define NT (s_loc ? s_vn : s_ex.n)
static const char *tok_at(int i) { return s_loc ? s_vt[i] : s_ex.tok[i]; }

static int ccpy(char *d, size_t cap, const char *s, size_t n)
{
    if (n + 1 > cap) return 0;
    memcpy(d, s, n);
    d[n] = 0;
    return 1;
}

/* Anh>Viet: tach hint tieng Viet theo khoang trang thanh the; false -> dung the tieng Anh */
static bool vi_build(void)
{
    const char *h = s_ex.hint;
    size_t len = strlen(h);
    while (len && strchr(" .?!,;:", h[len - 1])) len--;
    int n = 0;
    size_t i = 0;
    while (i < len) {
        while (i < len && h[i] == ' ') i++;
        size_t j = i;
        while (j < len && h[j] != ' ') j++;
        if (j > i) {
            if (n >= FD_SEN_TOK || !ccpy(s_vans[n], FD_SEN_TOK_LEN, h + i, j - i)) return false;
            n++;
        }
        i = j;
    }
    if (n < 2) return false;
    s_vn = n;
    for (int t = 0; t < 4; t++) {   /* xao, tranh trung thu tu dung */
        int ix[FD_SEN_TOK];
        for (int k = 0; k < n; k++) ix[k] = k;
        for (int k = n - 1; k > 0; k--) { int r = (int)(esp_random() % (uint32_t)(k + 1)), x = ix[k]; ix[k] = ix[r]; ix[r] = x; }
        bool same = true;
        for (int k = 0; k < n; k++) {
            strcpy(s_vt[k], s_vans[ix[k]]);
            if (strcmp(s_vt[k], s_vans[k])) same = false;
        }
        if (!same) break;
    }
    return true;
}

/* Anh>Viet: cham tai may; ghi ket qua cho fd_sen bang thu tu the tieng Anh tuong ung dung/sai */
static bool vi_answer(uint32_t ms)
{
    bool ok = true;
    for (int k = 0; k < s_vn; k++)
        if (strcmp(s_vt[s_pick[k]], s_vans[k])) ok = false;
    int ord[FD_SEN_TOK];
    bool used[FD_SEN_TOK] = {0};
    int n = s_ex.n;
    for (int k = 0; k < n; k++) ord[k] = k;
    if (ok) {
        for (int k = 0; k < n; k++)
            for (int j = 0; j < n; j++)
                if (!used[j] && !strcmp(s_ex.tok[j], s_ex.ans[k])) { ord[k] = j; used[j] = true; break; }
    } else if (n >= 2) {
        bool same = true;
        for (int k = 0; k < n; k++) if (strcmp(s_ex.tok[k], s_ex.ans[k])) same = false;
        if (same) { ord[0] = 1; ord[1] = 0; }
    }
    fd_sen_answer(s_pos, ord, n, ms);
    return ok;
}

static void ans_text(char *out, size_t cap)
{
    size_t o = 0;
    out[0] = 0;
    for (int k = 0; k < s_npick && o + 1 < cap; k++)
        o += snprintf(out + o, cap - o, "%s%s", k ? " " : "", tok_at(s_pick[k]));
    if (s_npick == 0) snprintf(out, cap, "%s", "…");
}

static void tile_style(int i)
{
    lv_obj_t *b = s_tile[i];
    if (!b) return;
    lv_obj_set_hidden(b, s_used[i]);   /* the da chon chuyen len vung cau */
    tile_box(b, i == s_cur);
}

static void next_free(int dir)
{
    for (int k = 0; k < NT; k++) {
        s_cur = (s_cur + dir + NT) % NT;
        if (!s_used[s_cur]) return;
    }
}

static void refresh_play(void)
{
    if (s_ans) {
        lv_obj_clean(s_ans);
        /* 2.3.4: bo nhan goi y SEN_TAP_HINT khi chua chon the */
        for (int k = 0; k < s_npick; k++) {
            lv_obj_t *t = lv_obj_create(s_ans);
            lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_scrollable(t, false);
            lv_obj_set_clickable(t, false);   /* cham roi vao vung cau = xoa tu cuoi */
            tile_box(t, false);
            lv_obj_set_style_bg_color(t, lv_color_hex(0xF3F4F6), 0);
            lv_obj_set_clickable(tile_label(t, tok_at(s_pick[k])), false);
        }
    }
    for (int i = 0; i < NT; i++) tile_style(i);
}

static void show_feed(bool ok);

static void feed_async(void *ok) { if (s_body) show_feed(ok != NULL); }

static void pick(int i)
{
    if (s_view != V_PLAY || i < 0 || i >= NT || s_used[i] || s_npick >= NT) return;
    s_used[i] = true;
    s_pick[s_npick++] = i;
    if (s_npick >= NT) {
        uint32_t ms = (uint32_t)((esp_timer_get_time() - s_t0) / 1000);
        bool ok = s_loc ? vi_answer(ms) : fd_sen_answer(s_pos, s_pick, s_npick, ms);
        lv_async_call(feed_async, (void *)(intptr_t)ok);   /* khong xoa the trong chinh callback cua no */
        return;
    }
    if (s_used[s_cur]) next_free(1);
    refresh_play();
}

static void undo(void)
{
    if (s_view != V_PLAY || s_npick == 0 || s_npick >= NT) return;   /* du the: dang cham */
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
    s_loc = s_vi && vi_build();
    s_view = V_PLAY;
    s_npick = s_cur = 0;
    memset(s_used, 0, sizeof(s_used));
    body_reset();
    lv_obj_set_scrollable(s_body, true);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), _("SEN_PROGRESS"), s_pos + 1, s_total);
    label(s_body, tmp, &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    const char *pr = s_loc ? s_ex.sentence : s_ex.hint;   /* cau nguon theo huong */
    if (pr[0]) label(s_body, pr, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    fd_sen_status_t st;
    fd_sen_get(&st);
    if (st.source != FD_SEN_SRC_ONLINE) label(s_body, _("SEN_OFFLINE"), &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    s_ans = row(s_body, true);
    lv_obj_set_style_min_height(s_ans, 36, 0);
    lv_obj_set_style_border_width(s_ans, 1, 0);
    lv_obj_set_style_border_side(s_ans, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_ans, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_bottom(s_ans, 4, 0);
    lv_obj_set_clickable(s_ans, true);
    lv_obj_add_event_cb(s_ans, undo_cb, LV_EVENT_CLICKED, NULL);   /* cham cau = xoa the cuoi */
    lv_obj_t *pool = row(s_body, true);
    for (int i = 0; i < NT; i++) {
        lv_obj_t *b = lv_button_create(pool);
        tile_box(b, false);
        tile_label(b, tok_at(i));
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
    lv_obj_t *c = content();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(c, ok ? _("SEN_CORRECT") : _("SEN_WRONG"), &fd_font_noto_18, ok ? C_OK : C_BAD);
    if (!ok) label(c, said, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(c, _("SEN_ANSWER_IS"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(c, s_loc ? s_ex.hint : s_ex.sentence, &fd_font_noto_18, FD_COLOR_TEXT);
    const char *sub = s_loc ? s_ex.sentence : s_ex.hint;
    if (sub[0]) label(c, sub, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    if (s_reps && s_ex.sentence[0]) {   /* 2.3.6: tu dong phat nhu Quiz (Tat/1/2 lan) */
        const fd_pron_cfg_t pc = {FD_PRON_PICO_ONLY, s_reps, false};
        ui_pron_say_cfg(s_ex.sentence, &pc);
    }
    lv_obj_t *r = row(s_body, false);
    button(r, _("SEN_CONTINUE"), next_cb);
}

static void show_result(void)
{
    s_view = V_RESULT;
    fd_sen_submit(s_best, (uint32_t)((esp_timer_get_time() - s_t_all) / 1000));
    body_reset();
    lv_obj_t *c = content();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), _("SEN_RESULT"), s_correct, s_total);
    label(c, tmp, &fd_font_noto_18, s_correct * 2 >= s_total ? C_OK : C_BAD);
    fd_sen_status_t st;
    fd_sen_get(&st);
    if (st.outbox > 0 && !fd_net_is_connected()) label(c, _("SEN_SYNC_LATER"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = row(s_body, false);
    button(r, _("BACK"), home_cb);
    button(r, _("SEN_AGAIN"), retry_cb);
}

/* ---------- screen ---------- */
static bool s_gate;

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    s_gate = !fd_ota_is_paired();   /* 2.3.0: can ghep (nhu Quiz) */
    if (s_gate) { ui_pair_gate(s_body); return; }
    prefs_load();
    show_setup();
}

static void destroy(void) { s_body = s_ans = s_cnt_lbl = s_dir_lbl = NULL; memset(s_tile, 0, sizeof(s_tile)); }

static void on_tick(void) { if (s_gate) return; poll_load(); fd_sen_sync(); }   /* co mang: gui ket qua dang cho */

static bool on_button(const fd_evt_button_t *b)
{
    if (b->action != FD_BTN_CLICK || s_gate) return false;
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
    if (s_view == V_RESULT) { show_setup(); return true; }
    if (s_view == V_SETUP) { start(); return true; }
    return false;
}

const ui_screen_t g_scr_sentence = {
    .name = "sentence", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = on_tick,
    .on_button = on_button,
};
