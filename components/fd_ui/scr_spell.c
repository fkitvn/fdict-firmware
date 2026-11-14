/* 4.5.3 Chinh ta (Spelling): ghep tu tu cac ky tu da xao, giong Sap xep cau nhung theo ky tu.
 * Moi the la 1 ky tu (UTF-8, chu tieng Viet co dau la 1 ky tu). Khoang trang giua cac tu khong thanh the,
 * vung tra loi tu chen khoang cach. Cham the de chon lan luot, cham vung tra loi de xoa ky tu cuoi.
 * Chon du the thi cham ngay. Chi tinh lan ghep dau (dung hoac sai), giong Sap xep cau va Dien tu.
 * Chieu: Ghep Anh (hien nghia tieng Viet), Ghep Viet (hien tu tieng Anh), Tron (xen ke tung the).
 * Ket qua: fd_sync_event("spell") luu events.log, dong bo cung cac module khac. Hoc offline tu kho dong bo. */
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "fd_voice.h"
#include "nvs.h"
#include "ui_priv.h"

#define C_OK  0x16A34A
#define C_BAD 0xDC2626
#define SP_CNT_KEY "spl_cnt"   /* NVS fd: so the/phien 10|20|0=Vo tan */
#define SP_DIR_KEY "spl_dir"   /* NVS fd: 0 Ghep Anh, 1 Ghep Viet, 2 Tron */
#define SP_REP_KEY "spl_rep"   /* NVS fd: doc tu tieng Anh sau moi the 0 Tat / 1 Bat */
#define SP_TOK FD_SPELL_TOK
#define SP_CH  8               /* 1 ky tu UTF-8 + dau ket hop */
#define SP_MAX 100

typedef enum { V_SETUP, V_MSG, V_PLAY, V_FEED, V_RESULT } view_t;

static view_t s_view;
static lv_obj_t *s_body, *s_ans, *s_dlg;
static lv_obj_t *s_tile[SP_TOK];
static fd_spell_word_t *s_words;
static int s_n, s_pos, s_correct, s_cnt = 10;
static uint8_t s_dir, s_reps;
static bool s_gate;
static bool s_build_vi;                 /* the hien tai: ghep tieng Viet */
static char s_tok[SP_TOK][SP_CH];       /* the da xao */
static char s_want[SP_TOK][SP_CH];      /* thu tu dung */
static bool s_brk[SP_TOK];              /* co khoang trang truoc ky tu thu k cua dap an */
static int s_nt;
static int s_pick[SP_TOK], s_npick;
static bool s_used[SP_TOK];

static void show_setup(void);
static void show_play(void);
static void show_result(void);

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

static void prefs_load(void)
{
    nvs_handle_t h;
    uint8_t c = 10, d = 0, r = 0;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, SP_CNT_KEY, &c);
        nvs_get_u8(h, SP_DIR_KEY, &d);
        nvs_get_u8(h, SP_REP_KEY, &r);
        nvs_close(h);
    }
    s_cnt = (c == 0 || c == 20) ? c : 10;
    s_dir = d <= 2 ? d : 0;
    s_reps = r ? 1 : 0;
}

static void pref_save(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

/* Tach chuoi thanh ky tu UTF-8 (gom dau ket hop U+0300..U+036F vao ky tu truoc). Bo khoang trang, ghi brk.
 * Tra so ky tu, -1 neu qua dai hoac loi. */
static int split_chars(const char *s, char out[][SP_CH], bool *brk, int cap)
{
    int n = 0;
    bool space = false;
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    while (*p) {
        if (*p == ' ' || *p == '\t') { space = n > 0; p++; continue; }
        int len = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 1;
        for (int k = 1; k < len; k++) if ((p[k] & 0xC0) != 0x80) { len = k; break; }
        /* dau ket hop: CC 80..CD AF */
        while ((p[len] == 0xCC && (p[len + 1] & 0xC0) == 0x80) || (p[len] == 0xCD && p[len + 1] >= 0x80 && p[len + 1] <= 0xAF)) len += 2;
        if (n >= cap || len + 1 > SP_CH) return -1;
        memcpy(out[n], p, (size_t)len);
        out[n][len] = 0;
        brk[n] = space;
        space = false;
        n++;
        p += len;
    }
    return n;
}

static int chars_of(const char *s)
{
    char tmp[SP_TOK][SP_CH];
    bool b[SP_TOK];
    return split_chars(s, tmp, b, SP_TOK);
}

static bool fits(const char *s) { int n = chars_of(s); return n >= 2 && n <= SP_TOK; }

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
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
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
    lv_obj_set_style_pad_column(r, 4, 0);
    lv_obj_set_style_pad_row(r, 4, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

static void tile_box(lv_obj_t *b)
{
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_pad_hor(b, 6, 0);
    lv_obj_set_style_pad_ver(b, 3, 0);
    lv_obj_set_style_min_width(b, 26, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), 0);
}

static lv_obj_t *tile_label(lv_obj_t *b, const char *t)
{
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    return l;
}

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

/* ---------- SETUP ---------- */
static void home_cb(lv_event_t *e) { (void)e; ui_nav_home(); }
static void retry_cb(lv_event_t *e) { (void)e; show_setup(); }
static void setup_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_setup(); }
enum { OPT_CNT, OPT_DIR, OPT_REP };
static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_CNT) { static const int NOPT[3] = {10, 20, 0}; s_cnt = NOPT[v < 3 ? v : 0]; pref_save(SP_CNT_KEY, (uint8_t)s_cnt); }
    else if (grp == OPT_DIR) { s_dir = (uint8_t)(v <= 2 ? v : 0); pref_save(SP_DIR_KEY, s_dir); }
    else if (grp == OPT_REP) { s_reps = v ? 1 : 0; pref_save(SP_REP_KEY, s_reps); }
    lv_async_call(setup_async, NULL);
}

static void start(void);
static void begin_cb(lv_event_t *e) { (void)e; start(); }

static void show_setup(void)
{
    fd_sync_session_end();
    s_view = V_SETUP;
    body_reset();
    lv_obj_t *sc = ui_setup_begin(s_body, _("SPELLING"));
    ui_setup_deck(sc, setup_async);
    static const char *const CNT[] = {"10", "20", "ENDLESS"};
    static const char *const DIR[] = {"SPELL_BUILD_EN", "SPELL_BUILD_VI", "SPELL_DIR_MIX"};
    static const char *const RP[] = {"OFF", "ON"};
    int nsel = s_cnt == 0 ? 2 : (s_cnt == 20 ? 1 : 0);
    ui_setup_opt_row(sc, _("QUESTIONS"), CNT, 3, nsel, opt_cb, OPT_CNT, 0);
    ui_setup_opt_row(sc, _("SEN_DIR"), DIR, 3, s_dir, opt_cb, OPT_DIR, 0);
    ui_setup_opt_row(sc, _("AUTO_PLAY"), RP, 2, s_reps, opt_cb, OPT_REP, UI_OPT_WIDE);
    ui_setup_start(s_body, begin_cb);
}

static void show_msg(void)
{
    s_view = V_MSG;
    body_reset();
    lv_obj_t *c = content();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(c, _("NO_OFFLINE_DATA_YET"), &fd_font_noto_18, FD_COLOR_TEXT);
    label(c, _("SYNC_AUTO_HINT"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = row(s_body, false);
    button(r, _("BACK"), retry_cb);
}

static void start(void)
{
    if (fd_sync_pick_count(0) <= 0) { fd_ui_toast(_("NO_DECK_WORDS")); return; }
    if (!s_words) s_words = ps_alloc(SP_MAX * sizeof(fd_spell_word_t));
    if (!s_words) { show_msg(); return; }
    int cap = s_cnt == 0 ? SP_MAX : s_cnt;
    int got = fd_sync_build_spell(s_words, SP_MAX);
    /* Chi giu tu ghep duoc it nhat mot chieu */
    int n = 0;
    for (int i = 0; i < got && n < cap; i++) {
        if (!fits(s_words[i].en) && !fits(s_words[i].vi)) continue;
        if (n != i) s_words[n] = s_words[i];
        n++;
    }
    s_n = n;
    if (s_n <= 0) { show_msg(); return; }
    s_pos = s_correct = 0;
    fd_sync_session_begin("spell");
    show_play();
}

/* ---------- PLAY ---------- */
static void prepare(void)
{
    const fd_spell_word_t *w = &s_words[s_pos];
    bool vi = s_dir == 1 || (s_dir == 2 && (s_pos & 1));
    if (vi && !fits(w->vi)) vi = false;
    if (!vi && !fits(w->en)) vi = true;
    s_build_vi = vi;
    s_nt = split_chars(vi ? w->vi : w->en, s_want, s_brk, SP_TOK);
    if (s_nt < 0) s_nt = 0;
    int ix[SP_TOK];
    for (int t = 0; t < 4; t++) {   /* xao, tranh trung thu tu dung */
        for (int k = 0; k < s_nt; k++) ix[k] = k;
        for (int k = s_nt - 1; k > 0; k--) { int r = (int)(esp_random() % (uint32_t)(k + 1)), x = ix[k]; ix[k] = ix[r]; ix[r] = x; }
        bool same = true;
        for (int k = 0; k < s_nt; k++) {
            strcpy(s_tok[k], s_want[ix[k]]);
            if (strcmp(s_tok[k], s_want[k])) same = false;
        }
        if (!same) break;
    }
}

static void refresh_play(void)
{
    if (s_ans) {
        lv_obj_clean(s_ans);
        for (int k = 0; k < s_npick; k++) {
            if (s_brk[k]) {   /* khoang cach giua hai tu */
                lv_obj_t *sp = lv_obj_create(s_ans);
                lv_obj_remove_style_all(sp);
                lv_obj_set_size(sp, 10, 4);
                lv_obj_set_clickable(sp, false);
            }
            lv_obj_t *t = lv_obj_create(s_ans);
            lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_scrollable(t, false);
            lv_obj_set_clickable(t, false);   /* cham vung tra loi = xoa ky tu cuoi */
            tile_box(t);
            lv_obj_set_style_bg_color(t, lv_color_hex(0xF3F4F6), 0);
            lv_obj_set_clickable(tile_label(t, s_tok[s_pick[k]]), false);
        }
    }
    for (int i = 0; i < s_nt; i++) if (s_tile[i]) lv_obj_set_hidden(s_tile[i], s_used[i]);
}

static void show_feed(bool ok);
static void feed_async(void *ok) { if (s_body) show_feed(ok != NULL); }

static void pick(int i)
{
    if (s_view != V_PLAY || i < 0 || i >= s_nt || s_used[i] || s_npick >= s_nt) return;
    s_used[i] = true;
    s_pick[s_npick++] = i;
    if (s_npick >= s_nt) {
        bool ok = true;
        for (int k = 0; k < s_nt; k++) if (strcmp(s_tok[s_pick[k]], s_want[k])) ok = false;
        fd_sync_event("spell", s_words[s_pos].wid, ok);   /* lan ghep dau tien */
        lv_async_call(feed_async, (void *)(intptr_t)ok);   /* khong xoa the trong chinh callback cua no */
        return;
    }
    refresh_play();
}

static void undo(void)
{
    if (s_view != V_PLAY || s_npick == 0 || s_npick >= s_nt) return;
    s_used[s_pick[--s_npick]] = false;
    refresh_play();
}

static void tile_cb(lv_event_t *e) { pick((int)(intptr_t)lv_event_get_user_data(e)); }
static void undo_cb(lv_event_t *e) { (void)e; undo(); }

static void show_play(void)
{
    prepare();
    if (s_nt < 2) { if (++s_pos < s_n) show_play(); else show_result(); return; }
    s_view = V_PLAY;
    s_npick = 0;
    memset(s_used, 0, sizeof(s_used));
    body_reset();
    lv_obj_set_scrollable(s_body, true);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    const fd_spell_word_t *w = &s_words[s_pos];
    char tmp[32];
    if (s_cnt == 0) snprintf(tmp, sizeof(tmp), _("QUIZ_QUESTION_N"), s_pos + 1);
    else snprintf(tmp, sizeof(tmp), _("SEN_PROGRESS"), s_pos + 1, s_n);
    label(s_body, tmp, &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    label(s_body, s_build_vi ? _("SPELL_PROMPT_VI") : _("SPELL_PROMPT_EN"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(s_body, s_build_vi ? w->en : w->vi, &fd_font_noto_18, FD_COLOR_TEXT);
    s_ans = row(s_body, true);
    lv_obj_set_style_min_height(s_ans, 36, 0);
    lv_obj_set_style_border_width(s_ans, 1, 0);
    lv_obj_set_style_border_side(s_ans, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_ans, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_bottom(s_ans, 4, 0);
    lv_obj_set_clickable(s_ans, true);
    lv_obj_add_event_cb(s_ans, undo_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pool = row(s_body, true);
    for (int i = 0; i < s_nt; i++) {
        lv_obj_t *b = lv_button_create(pool);
        tile_box(b);
        tile_label(b, s_tok[i]);
        lv_obj_add_event_cb(b, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_tile[i] = b;
    }
    refresh_play();
}

/* ---------- FEEDBACK / RESULT ---------- */
static void next(void)
{
    if (++s_pos < s_n) show_play();
    else show_result();
}
static void next_cb(lv_event_t *e) { (void)e; next(); }

static void show_feed(bool ok)
{
    s_view = V_FEED;
    if (ok) s_correct++;
    const fd_spell_word_t *w = &s_words[s_pos];
    body_reset();
    lv_obj_t *c = content();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(c, ok ? _("SEN_CORRECT") : _("SEN_WRONG"), &fd_font_noto_18, ok ? C_OK : C_BAD);
    label(c, _("SEN_ANSWER_IS"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(c, s_build_vi ? w->vi : w->en, &fd_font_noto_18, FD_COLOR_TEXT);
    label(c, s_build_vi ? w->en : w->vi, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    if (w->ipa[0]) label(c, w->ipa, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    if (s_reps && w->en[0]) {
        const fd_pron_cfg_t pc = {FD_PRON_PICO_ONLY, 1, false};
        ui_pron_say_cfg(w->en, &pc);
    }
    lv_obj_t *r = row(s_body, false);
    button(r, _("SEN_CONTINUE"), next_cb);
}

static void show_result(void)
{
    fd_sync_session_end();
    s_view = V_RESULT;
    body_reset();
    lv_obj_t *c = content();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    int total = s_pos < s_n ? s_pos : s_n;
    char tmp[48];
    snprintf(tmp, sizeof(tmp), _("SEN_RESULT"), s_correct, total);
    label(c, _("SPELLING"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    label(c, tmp, &fd_font_mont_32, total == 0 ? FD_COLOR_TEXT_MUTED : s_correct * 2 >= total ? C_OK : C_BAD);
    lv_obj_t *r = row(s_body, false);
    button(r, _("BACK"), home_cb);
    button(r, _("SEN_AGAIN"), retry_cb);
}

/* ---------- thoat giua bai ---------- */
static void dlg_close(void)
{
    if (s_dlg) {
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
}

static void quit_spell(void)
{
    int done = s_view == V_FEED ? s_pos + 1 : s_pos;
    if (s_cnt == 0 && done > 0) { s_pos = done; show_result(); return; }
    show_setup();
}

static void dlg_cb(lv_event_t *e)
{
    bool ok = (bool)(intptr_t)lv_event_get_user_data(e);
    dlg_close();
    if (ok) quit_spell();
}

static void dlg_btn(lv_obj_t *r, const char *t, bool primary)
{
    lv_obj_t *b = lv_button_create(r);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, dlg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)primary);
}

static void dlg_open(void)
{
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dlg);
    lv_obj_set_size(s_dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_40, 0);
    lv_obj_set_clickable(s_dlg, true);
    lv_obj_t *box = lv_obj_create(s_dlg);
    lv_obj_set_size(box, lv_pct(88), LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 14, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    label(box, _("QUIT_THIS_QUIZ"), &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_t *r = row(box, false);
    lv_obj_set_style_pad_column(r, 10, 0);
    lv_obj_set_style_pad_top(r, 4, 0);
    dlg_btn(r, _("CANCEL"), false);
    dlg_btn(r, _("QUIT"), true);
}

/* ---------- screen ---------- */
static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    prefs_load();
    show_setup();
}

static void destroy(void)
{
    dlg_close();
    fd_sync_session_end();
    s_body = s_ans = NULL;
    memset(s_tile, 0, sizeof(s_tile));
}

static bool on_button(const fd_evt_button_t *b)
{
    if (b->action != FD_BTN_CLICK || s_gate) return false;
    if (s_dlg) return true;
    if (s_view == V_FEED) { next(); return true; }
    if (s_view == V_RESULT) { show_setup(); return true; }
    if (s_view == V_SETUP) { start(); return true; }
    return false;
}

static bool on_back(void)
{
    if (s_gate) return false;
    if (s_dlg) { dlg_close(); return true; }
    if (s_view == V_PLAY || s_view == V_FEED) { dlg_open(); return true; }
    if (s_view != V_SETUP) { show_setup(); return true; }
    return false;
}

const ui_screen_t g_scr_spell = {
    .name = "spell", .parent = FD_SCR_HOME, .build = build, .destroy = destroy,
    .on_button = on_button, .on_back = on_back,
};
