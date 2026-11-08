#include "esp_attr.h"
/* 2.3.5: Tu dien online qua dict.fkit.vn; lich su 30; auto-add + hang cho. */
#include "ui_priv.h"
#include "fd_dictnet.h"
#include "fd_fonts.h"
#include "fd_i18n.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_t9kb.h"
#include "lvgl.h"
#include <ctype.h>
#include <string.h>

static lv_obj_t *s_body, *s_ta;
static lv_timer_t *s_poll;
static bool s_gate;
static char s_word[FD_DICTNET_WORD_LEN];

static void show_home(void);
static lv_obj_t *s_kb;   /* 4.1.5: ban phim (an/hien) */
static void show_result_ok(const fd_dictnet_status_t *st);
static void show_error(const fd_dictnet_status_t *st);

static void stop_poll(void)
{
    if (s_poll) { lv_timer_delete(s_poll); s_poll = NULL; }
}

static void body_reset(void)
{
    s_kb = NULL;
    stop_poll();
    s_ta = NULL;
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

static void poll_cb(lv_timer_t *t);

static void begin_lookup(const char *word)
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
    if (!fd_dictnet_lookup(s_word)) {
        fd_dictnet_status_t st;
        fd_dictnet_get(&st);
        if (st.state == FD_DICTNET_ERROR) show_error(&st);
        return;
    }
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(s_body, _("LOADING"), &fd_font_noto_18, FD_COLOR_TEXT);
    s_poll = lv_timer_create(poll_cb, 200, NULL);
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    fd_dictnet_status_t st;
    fd_dictnet_get(&st);
    if (st.state == FD_DICTNET_LOOKUP || st.state == FD_DICTNET_ADD) return;
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

static void hist_cb(lv_event_t *e)
{
    const char *w = (const char *)lv_event_get_user_data(e);
    begin_lookup(w);
}

static void show_home(void)
{
    body_reset();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    label(s_body, _("DICTIONARY"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);

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
    lv_textarea_set_accepted_chars(s_ta, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ");   /* chi chu + cach */
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
            snprintf(line, sizeof(line), "%.*s - %.*s", FD_DICTNET_WORD_LEN - 1, hist[i].en, FD_DICTNET_MEAN_LEN - 1, hist[i].vi);
            lv_obj_t *l = lv_label_create(b);
            lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_set_width(l, lv_pct(100));
            lv_label_set_text(l, line);
            lv_obj_center(l);
            /* user_data must outlive; copy into static hist buffer via strdup-like static store */
            static char words[FD_DICTNET_HIST_MAX][FD_DICTNET_WORD_LEN];
            strlcpy(words[i], hist[i].en, sizeof(words[i]));
            lv_obj_add_event_cb(b, hist_cb, LV_EVENT_CLICKED, words[i]);
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
