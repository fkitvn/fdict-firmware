#include "esp_attr.h"
/* 2.3.5: Tu dien online qua dict.fkit.vn; lich su 30; auto-add + hang cho. */
#include "ui_priv.h"
#include "fd_dictnet.h"
#include "fd_fonts.h"
#include "fd_i18n.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_t9kb.h"
#include "fd_telex.h"
#include <stdint.h>
#include "lvgl.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_body, *s_ta, *s_load_lbl;
static lv_timer_t *s_poll;
static uint8_t s_load_dots;
static bool s_load_adding;
static bool s_gate;
static char s_word[FD_DICTNET_WORD_LEN];
static uint8_t s_dir = FD_DICTNET_DIR_EN_VI;   /* 4.1.8: Anh-Viet / Viet-Anh */
static bool s_telex_busy;

static void show_home(void);
static lv_obj_t *s_kb;   /* 4.1.5: ban phim (an/hien) */
static void show_result_ok(const fd_dictnet_status_t *st);
static void show_error(const fd_dictnet_status_t *st);
static void poll_cb(lv_timer_t *t);

static void stop_poll(void)
{
    if (s_poll) { lv_timer_delete(s_poll); s_poll = NULL; }
}

static void body_reset(void)
{
    s_kb = NULL;
    stop_poll();
    s_ta = NULL;
    s_load_lbl = NULL;
    if (s_body) lv_obj_clean(s_body);
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, t ? t : "");
    return l;
}

static void trim_word(char *s)
{
    char *a = s;
    while (*a && isspace((unsigned char)*a)) a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') *p = (char)(c - 'A' + 'a');
    }
}

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    show_home();
}

static void load_status_apply(void)
{
    if (!s_load_lbl) return;
    if (s_load_adding) {
        lv_label_set_text(s_load_lbl, _("LOOKUP_ADDING"));
        return;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%.*s", _("LOOKUP_SEARCHING"), (int)(s_load_dots % 4), "...");
    lv_label_set_text(s_load_lbl, buf);
}

static void magnifier_anim_cb(void *obj, int32_t v)
{
    lv_obj_t *g = obj;
    int32_t x = ((v - 500) * 14) / 500;
    int32_t up = v <= 500 ? v : 1000 - v;
    int32_t y = 5 - (up * 10) / 500;
    lv_obj_set_style_translate_x(g, x, 0);
    lv_obj_set_style_translate_y(g, y, 0);
}

static void show_loading(void)
{
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 10, 0);

    /* Kinh lup ve bang hinh LVGL (khong anh): kinh tron + canh nghieng, dao nhe nhu dang so. */
    lv_obj_t *glass = lv_obj_create(s_body);
    lv_obj_remove_style_all(glass);
    lv_obj_set_size(glass, 76, 76);
    lv_obj_set_scrollable(glass, false);
    lv_obj_set_style_clip_corner(glass, false, 0);

    lv_obj_t *lens = lv_obj_create(glass);
    lv_obj_remove_style_all(lens);
    lv_obj_set_size(lens, 44, 44);
    lv_obj_set_pos(lens, 10, 6);
    lv_obj_set_scrollable(lens, false);
    lv_obj_set_style_radius(lens, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(lens, 4, 0);
    lv_obj_set_style_border_color(lens, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_color(lens, lv_color_hex(FD_COLOR_PRIMARY_TINT), 0);
    lv_obj_set_style_bg_opa(lens, LV_OPA_80, 0);

    lv_obj_t *shine = lv_obj_create(lens);
    lv_obj_remove_style_all(shine);
    lv_obj_set_size(shine, 10, 10);
    lv_obj_set_pos(shine, 8, 7);
    lv_obj_set_scrollable(shine, false);
    lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shine, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(shine, LV_OPA_COVER, 0);

    lv_obj_t *handle = lv_obj_create(glass);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, 22, 6);
    lv_obj_set_pos(handle, 44, 46);
    lv_obj_set_scrollable(handle, false);
    lv_obj_set_style_radius(handle, 3, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(FD_COLOR_PRIMARY_DARK), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(handle, 0, 0);
    lv_obj_set_style_transform_pivot_y(handle, 3, 0);
    lv_obj_set_style_transform_rotation(handle, 450, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, glass);
    lv_anim_set_exec_cb(&a, magnifier_anim_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1300);
    lv_anim_set_reverse_duration(&a, 1300);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    s_load_adding = false;
    s_load_dots = 0;
    s_load_lbl = label(s_body, _("LOOKUP_SEARCHING"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_style_text_align(s_load_lbl, LV_TEXT_ALIGN_CENTER, 0);

    if (s_word[0]) {
        const lv_font_t *wf = s_dir == FD_DICTNET_DIR_EN_VI ? &fd_font_mont_32 : &fd_font_noto_18;
        lv_obj_t *chip = lv_obj_create(s_body);
        lv_obj_remove_style_all(chip);
        lv_obj_set_width(chip, lv_pct(100));
        lv_obj_set_height(chip, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(chip, lv_color_hex(FD_COLOR_PRIMARY_TINT), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 10, 0);
        lv_obj_set_style_pad_hor(chip, 12, 0);
        lv_obj_set_style_pad_ver(chip, 8, 0);
        lv_obj_set_scrollable(chip, false);
        lv_obj_t *w = lv_label_create(chip);
        lv_obj_set_style_text_font(w, wf, 0);
        lv_obj_set_style_text_color(w, lv_color_hex(FD_COLOR_TEXT), 0);
        lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(w, lv_pct(100));
        lv_label_set_long_mode(w, LV_LABEL_LONG_DOT);
        lv_label_set_text(w, s_word);
    }

    s_poll = lv_timer_create(poll_cb, 200, NULL);
}

static void begin_lookup_dir(const char *word, uint8_t dir)
{
    char buf[FD_DICTNET_WORD_LEN];
    strlcpy(buf, word ? word : "", sizeof(buf));
    trim_word(buf);
    if (!buf[0]) return;
    strlcpy(s_word, buf, sizeof(s_word));
    if (!fd_net_is_connected()) {
        fd_dictnet_status_t st = {0};
        st.state = FD_DICTNET_ERROR;
        st.err = FD_DICTNET_ERR_NO_WIFI;
        show_error(&st);
        return;
    }
    if (!fd_dictnet_lookup_dir(s_word, dir)) {
        fd_dictnet_status_t st;
        fd_dictnet_get(&st);
        if (st.state == FD_DICTNET_ERROR) show_error(&st);
        return;
    }
    show_loading();
}

static void begin_lookup(const char *word) { begin_lookup_dir(word, s_dir); }

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    fd_dictnet_status_t st;
    fd_dictnet_get(&st);
    if (st.state == FD_DICTNET_LOOKUP || st.state == FD_DICTNET_ADD) {
        bool adding = st.state == FD_DICTNET_ADD;
        if (adding != s_load_adding) { s_load_adding = adding; s_load_dots = 0; }
        else if (!adding) s_load_dots = (uint8_t)((s_load_dots + 1) % 4);
        load_status_apply();
        return;
    }
    stop_poll();
    if (st.state == FD_DICTNET_OK) show_result_ok(&st);
    else show_error(&st);
}

static void t9_done_cb(lv_obj_t *kb, const char *text, void *user_data)
{
    (void)kb;
    (void)user_data;
    begin_lookup(text);
}

static void show_error(const fd_dictnet_status_t *st)
{
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    const char *msg = _("LOOKUP_ERROR");
    if (st->err == FD_DICTNET_ERR_NO_WIFI) msg = _("LOOKUP_NEED_WIFI");
    else if (st->err == FD_DICTNET_ERR_UNPAIRED) msg = _("PAIR_TO_STUDY");
    else if (st->err == FD_DICTNET_ERR_NOT_FOUND) msg = _("LOOKUP_NOT_FOUND");
    else if (st->err == FD_DICTNET_ERR_NOMEM) msg = _("MEMORY_ERROR");
    else if (st->err == FD_DICTNET_ERR_STORAGE) msg = _("STORAGE_ERROR");
    label(s_body, msg, &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_t *b = lv_button_create(s_body);
    lv_obj_set_size(b, lv_pct(100), 40);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, _("BACK"));
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, back_home_cb, LV_EVENT_CLICKED, NULL);
}

static void show_result_ok(const fd_dictnet_status_t *st)
{
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    label(s_body, st->result.en, &fd_font_mont_32, FD_COLOR_TEXT);
    if (st->result.ipa[0]) label(s_body, st->result.ipa, NULL, FD_COLOR_PRIMARY_TEXT);
    if (st->result.pos[0]) label(s_body, st->result.pos, NULL, FD_COLOR_TEXT_MUTED);
    label(s_body, st->result.vi, &fd_font_noto_18, FD_COLOR_TEXT);
    if (st->added) label(s_body, _("LOOKUP_ADDED"), NULL, FD_COLOR_PRIMARY_TEXT);
    else if (st->queued) label(s_body, _("LOOKUP_QUEUED"), NULL, FD_COLOR_TEXT_MUTED);
    lv_obj_t *b = lv_button_create(s_body);
    lv_obj_set_size(b, lv_pct(100), 42);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, _("BACK"));
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, back_home_cb, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *s_kb;
static void kb_set_open(lv_obj_t *icon, bool open)
{
    if (s_kb) lv_obj_set_hidden(s_kb, !open);
    if (icon) lv_label_set_text(icon, open ? LV_SYMBOL_DOWN : LV_SYMBOL_KEYBOARD);
}
static void kb_toggle_cb(lv_event_t *e)
{
    kb_set_open((lv_obj_t *)lv_event_get_user_data(e), s_kb && lv_obj_is_hidden(s_kb));
}
static void kb_open_cb(lv_event_t *e) { kb_set_open((lv_obj_t *)lv_event_get_user_data(e), true); }

static char s_hwords[FD_DICTNET_HIST_MAX][FD_DICTNET_WORD_LEN];
static uint8_t s_hdirs[FD_DICTNET_HIST_MAX];
static void hist_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= FD_DICTNET_HIST_MAX) return;
    begin_lookup_dir(s_hwords[i], s_hdirs[i]);   /* tra lai theo dung chieu da luu */
}

/* Telex (chi o che do Viet - Anh): bien doi tu cuoi khi go 1 chu ASCII. Gia dinh con tro o cuoi (o nhap 1 dong). */
static void ta_insert_cb(lv_event_t *e)
{
    if (s_dir != FD_DICTNET_DIR_VI_EN || s_telex_busy) return;
    lv_obj_t *ta = lv_event_get_target(e);
    const char *in = (const char *)lv_event_get_param(e);
    if (!in || !in[0] || in[1]) return;
    char key = in[0];
    if (!((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z'))) return;
    const char *cur = lv_textarea_get_text(ta);
    int32_t cps = 0;
    for (const char *p = cur; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) cps++;
    if (lv_textarea_get_cursor_pos(ta) != cps) return;
    const char *sp = strrchr(cur, ' ');
    size_t pre = sp ? (size_t)(sp - cur + 1) : 0;
    uint32_t w[FD_DICTNET_WORD_LEN];
    int n = fd_telex_utf8_decode(cur + pre, w, FD_DICTNET_WORD_LEN - 1);
    int m = fd_telex_feed(w, n, FD_DICTNET_WORD_LEN - 1, key);
    if (m < 0) return;
    char out[FD_DICTNET_WORD_LEN * 3];
    if (pre >= sizeof(out)) return;
    memcpy(out, cur, pre);
    fd_telex_utf8_encode(w, m, out + pre, (int)(sizeof(out) - pre));
    if (strlen(out) >= FD_DICTNET_WORD_LEN) return;   /* vuot bo dem tu -> go binh thuong */
    s_telex_busy = true;
    lv_textarea_set_text(ta, out);
    s_telex_busy = false;
    lv_textarea_set_insert_replace(ta, "");   /* sau set_text: huy ky tu goc */
}

static void dir_apply(lv_obj_t *lbl)
{
    if (lbl) lv_label_set_text(lbl, s_dir == FD_DICTNET_DIR_VI_EN ? "Việt - Anh" : "Anh - Việt");
    if (!s_ta) return;
    /* Viet - Anh: bo loc ky tu (cho phep chu co dau tu Telex); Anh - Viet: chi a-z + cach */
    lv_textarea_set_accepted_chars(s_ta, s_dir == FD_DICTNET_DIR_VI_EN ? NULL : "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ");
}
static void dir_cb(lv_event_t *e)
{
    s_dir = s_dir == FD_DICTNET_DIR_VI_EN ? FD_DICTNET_DIR_EN_VI : FD_DICTNET_DIR_VI_EN;
    if (s_ta) lv_textarea_set_text(s_ta, "");
    dir_apply((lv_obj_t *)lv_event_get_user_data(e));
}

static void show_home(void)
{
    body_reset();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    /* 4.1.8: tieu de + nut chieu tra (Anh - Viet / Viet - Anh) tren cung hang */
    lv_obj_t *hdr = lv_obj_create(s_body);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* 4.1.9 FIX: label() dat rong 100% -> nut chieu tra bi day ra ngoai hang (khong cham duoc). Tieu de co gian, nut giu kich thuoc. */
    lv_obj_t *ttl = label(hdr, _("DICTIONARY"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_width(ttl, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ttl, 1);
    lv_label_set_long_mode(ttl, LV_LABEL_LONG_DOT);
    lv_obj_t *db = lv_button_create(hdr);
    lv_obj_set_size(db, LV_SIZE_CONTENT, 34);
    lv_obj_set_ext_click_area(db, 6);
    lv_obj_set_style_pad_hor(db, 10, 0);
    lv_obj_set_style_radius(db, 8, 0);
    lv_obj_set_style_shadow_width(db, 0, 0);
    lv_obj_set_style_bg_color(db, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *dl = lv_label_create(db);
    lv_obj_set_style_text_font(dl, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(dl, lv_color_white(), 0);
    lv_obj_center(dl);
    lv_obj_add_event_cb(db, dir_cb, LV_EVENT_CLICKED, dl);

    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);   /* 4.1.7: khe o nhap - nut thu gon */
    s_ta = lv_textarea_create(row);
    lv_obj_set_flex_grow(s_ta, 1);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, FD_DICTNET_WORD_LEN - 1);
    lv_obj_set_style_text_font(s_ta, &fd_font_noto_14, 0);   /* Noto: co dau tieng Viet cho goi y */
    lv_textarea_set_placeholder_text(s_ta, _("LOOKUP_HINT"));   /* 4.1.7: goi y nam trong o nhap (bo nhan phia tren) */
    lv_obj_set_style_pad_all(s_ta, 8, 0);

    /* 4.1.5: mac dinh ban phim QWERTY luon mo; nut thu gon de hien lich su tra, cham o nhap -> mo lai */
    lv_obj_t *tg = lv_button_create(row);
    lv_obj_set_size(tg, 40, 40);
    lv_obj_set_style_shadow_width(tg, 0, 0);
    lv_obj_set_style_radius(tg, 8, 0);
    lv_obj_t *tgl = lv_label_create(tg);
    lv_obj_set_style_text_font(tgl, &lv_font_montserrat_14, 0);   /* LV_SYMBOL_* chi co trong Montserrat (Noto -> o vuong) */
    lv_obj_center(tgl);
    s_kb = fd_t9kb_create(s_body, s_ta, t9_done_cb, NULL);
    if (s_kb) fd_t9kb_set_qwerty_plain(s_kb);   /* chi chu thuong + xoa/cach/OK */
    lv_obj_add_event_cb(s_ta, ta_insert_cb, LV_EVENT_INSERT, NULL);
    dir_apply(dl);
    lv_obj_add_event_cb(tg, kb_toggle_cb, LV_EVENT_CLICKED, tgl);
    lv_obj_add_event_cb(s_ta, kb_open_cb, LV_EVENT_CLICKED, tgl);
    kb_set_open(tgl, true);

    /* 2.3.6 FIX reboot: truoc day hist[30] (30 x 280 B = 8.4 KB) nam tren stack task LVGL (8 KB) -> tran stack -> reset.
     * Bo dem tinh trong PSRAM: khong ton stack hoac RAM noi. */
    static EXT_RAM_BSS_ATTR fd_dictnet_entry_t hist[FD_DICTNET_HIST_MAX];
    int n = fd_dictnet_history(hist, FD_DICTNET_HIST_MAX);
    if (n > 0) {
        label(s_body, _("LOOKUP_HISTORY"), NULL, FD_COLOR_TEXT_MUTED);
        lv_obj_t *list = lv_obj_create(s_body);
        lv_obj_remove_style_all(list);
        lv_obj_set_width(list, lv_pct(100));
        lv_obj_set_flex_grow(list, 1);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list, 4, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        for (int i = 0; i < n; i++) {
            lv_obj_t *b = lv_button_create(list);
            lv_obj_set_width(b, lv_pct(100));
            lv_obj_set_height(b, 36);
            lv_obj_set_style_bg_color(b, lv_color_white(), 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_style_radius(b, 8, 0);
            static char line[FD_DICTNET_WORD_LEN + FD_DICTNET_MEAN_LEN + 8];
            bool ve = hist[i].dir == FD_DICTNET_DIR_VI_EN;
            if (ve) snprintf(line, sizeof(line), "%.*s - %.*s", FD_DICTNET_MEAN_LEN - 1, hist[i].vi, FD_DICTNET_WORD_LEN - 1, hist[i].en);
            else snprintf(line, sizeof(line), "%.*s - %.*s", FD_DICTNET_WORD_LEN - 1, hist[i].en, FD_DICTNET_MEAN_LEN - 1, hist[i].vi);
            lv_obj_t *l = lv_label_create(b);
            lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_set_width(l, lv_pct(100));
            lv_label_set_text(l, line);
            lv_obj_center(l);
            /* user_data must outlive; copy into static hist buffer via strdup-like static store */
            strlcpy(s_hwords[i], ve ? hist[i].vi : hist[i].en, sizeof(s_hwords[i]));
            s_hdirs[i] = hist[i].dir;
            lv_obj_add_event_cb(b, hist_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    fd_dictnet_init();
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    show_home();
}

static void destroy(void)
{
    stop_poll();
    s_body = s_ta = NULL;
}

static bool on_back(void)
{
    if (s_gate) return false;
    if (s_poll) { stop_poll(); show_home(); return true; }
    return false;
}

const ui_screen_t g_scr_dict = {
    .name = "dict", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_back = on_back,
};
