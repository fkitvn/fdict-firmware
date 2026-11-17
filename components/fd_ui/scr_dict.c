#include "esp_attr.h"
/* 2.3.5: Tu dien online qua dict.fkit.vn; lich su. 4.5.4: tra + luu vao bo 1 lan goi, thoat thi dong bo. */
#include "ui_priv.h"
#include "fd_dictnet.h"
#include "fd_fonts.h"
#include "fd_i18n.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
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
static bool s_gate;
static bool s_at_home;   /* true = man nhap tu; vuot lui o man khac ve day, khong thoat Tu dien */
static char s_word[FD_DICTNET_WORD_LEN];
static char s_say[FD_DICTNET_WORD_LEN];  /* 4.3.2: tu tieng Anh de nut loa phat am */
static fd_dictnet_status_t s_shown;
static char s_col_line[320];
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
    s_at_home = false;
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

    s_load_dots = 0;
    s_load_lbl = label(s_body, _("LOOKUP_SEARCHING"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_style_text_align(s_load_lbl, LV_TEXT_ALIGN_CENTER, 0);

    if (s_word[0]) {
        const lv_font_t *wf = &fd_font_noto_18;
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
    show_loading();
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    fd_dictnet_status_t st;
    fd_dictnet_get(&st);
    if (st.state == FD_DICTNET_LOOKUP) {
        s_load_dots = (uint8_t)((s_load_dots + 1) % 4);
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
    s_at_home = false;
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

/* Phat am tu tieng Anh, giong Lat tu: Pico (va cache neu co), nut loa doc 1 lan. */
static void speak_cb(lv_event_t *e)
{
    (void)e;
    if (!s_say[0]) return;
    const fd_pron_cfg_t cfg = {FD_PRON_PICO_ONLY, 1, false};
    ui_pron_say_cfg(s_say, &cfg);
}

static void show_result_ok(const fd_dictnet_status_t *st)
{
    s_at_home = false;
    s_shown = *st;
    body_reset();
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    strlcpy(s_say, st->result.en, sizeof(s_say));

    /* Hang tieu de: tu trai, nut loa goc phai (kieu Lat tu: tron amber 48 px). */
    lv_obj_t *head = lv_obj_create(s_body);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 8, 0);
    lv_obj_set_scrollable(head, false);

    lv_obj_t *w = lv_label_create(head);
    lv_obj_set_style_text_font(w, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(w, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_obj_set_flex_grow(w, 1);
    lv_obj_set_width(w, 1);   /* flex gian phan con lai, khong day nut loa ra ngoai */
    lv_label_set_long_mode(w, LV_LABEL_LONG_WRAP);
    lv_label_set_text(w, st->result.en);

    if (s_say[0]) {
        lv_obj_t *spk = lv_button_create(head);
        lv_obj_set_size(spk, 48, 48);
        lv_obj_set_style_radius(spk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(spk, 0, 0);
        lv_obj_set_style_bg_opa(spk, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY), 0);
        lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY_DARK), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(spk, 0, 0);
        lv_obj_add_event_cb(spk, speak_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *si = lv_label_create(spk);
        lv_obj_set_style_text_font(si, &fd_font_mont_24, 0);
        lv_obj_set_style_text_color(si, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
        lv_label_set_text(si, LV_SYMBOL_VOLUME_MAX);
        lv_obj_center(si);
    }
    if (st->result.ipa[0]) label(s_body, st->result.ipa, &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    if (st->result.pos[0]) label(s_body, st->result.pos, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    label(s_body, st->result.vi, &fd_font_noto_18, FD_COLOR_TEXT);
    /* Da luu thi chi ghi ten bo, khong cho doi bo tai man ket qua. */
    if (!s_shown.added) {
        label(s_body, _("LOOKUP_NOT_SAVED"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
        return;
    }
    const char *cname = s_shown.collection_name;
    char saved_name[FD_DICTNET_CNAME_LEN];
    if (!cname[0]) {
        fd_dictnet_saved_collection(NULL, 0, saved_name, sizeof(saved_name));
        cname = saved_name;
    }
    if (cname[0]) {
        snprintf(s_col_line, sizeof(s_col_line), "%s\n%s", _("LOOKUP_ADDED_TO"), cname);
        label(s_body, s_col_line, &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    } else {
        label(s_body, _("LOOKUP_ADDED"), &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    }
}

static lv_obj_t *s_kb;
static void kb_set_open(bool open)
{
    if (s_kb) lv_obj_set_hidden(s_kb, !open);
}
static void kb_open_cb(lv_event_t *e)
{
    (void)e;
    kb_set_open(true);
}
static void search_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ta) return;
    begin_lookup(lv_textarea_get_text(s_ta));
}

static char s_hwords[FD_DICTNET_HIST_MAX][FD_DICTNET_WORD_LEN];
static void hist_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= FD_DICTNET_HIST_MAX) return;
    begin_lookup(s_hwords[i]);
}

/* Telex luon bat: ban phim chi co chu Latin, API tu nhan tieng Viet khi co dau. */
static void ta_insert_cb(lv_event_t *e)
{
    if (s_telex_busy) return;
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

static EXT_RAM_BSS_ATTR char s_dd_opts[FD_SYNC_MAX_COLS * (FD_SYNC_COL_NAME_LEN + 1)];
static char s_dd_ids[FD_SYNC_MAX_COLS][FD_SYNC_WID_LEN];
static int s_dd_n;

static void opt_add(const char *name)
{
    size_t used = strlen(s_dd_opts);
    if (s_dd_n && used + 1 < sizeof(s_dd_opts)) s_dd_opts[used++] = '\n';
    for (const char *p = name; *p && used + 1 < sizeof(s_dd_opts); p++)
        s_dd_opts[used++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
    s_dd_opts[used] = 0;
}

static bool find_col(const char *id, fd_sync_col_t *out)
{
    int nc = fd_sync_col_count();
    for (int i = 0; i < nc; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c) || !c.id[0]) continue;
        if (strcmp(c.id, id) == 0) { *out = c; return true; }
    }
    return false;
}

/* Tra ve chi so dang chon. -1 khi chua co bo nao. */
static int fill_dd(void)
{
    s_dd_n = 0;
    s_dd_opts[0] = 0;
    char saved[FD_DICTNET_CID_LEN];
    fd_dictnet_saved_collection(saved, sizeof(saved), NULL, 0);
    int sel = -1, def = -1;
    int nc = fd_sync_col_count();
    for (int i = 0; i < nc && s_dd_n < FD_SYNC_MAX_COLS; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c) || !c.id[0]) continue;
        opt_add(c.name[0] ? c.name : c.id);
        strlcpy(s_dd_ids[s_dd_n], c.id, sizeof(s_dd_ids[0]));
        if (saved[0] && !strcmp(saved, c.id)) sel = s_dd_n;
        if (def < 0 && c.def) def = s_dd_n;
        s_dd_n++;
    }
    if (!s_dd_n) return -1;
    if (sel < 0) {
        sel = def >= 0 ? def : 0;
        fd_sync_col_t c;
        if (find_col(s_dd_ids[sel], &c))
            fd_dictnet_set_collection(c.id, c.name[0] ? c.name : c.id);
    }
    return sel;
}

static void style_dd_list(lv_obj_t *dd)
{
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (!list) return;
    lv_obj_set_style_text_font(list, &fd_font_noto_14, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, &fd_font_noto_14, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list, lv_color_hex(FD_COLOR_PRIMARY), LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_max_height(list, 132, 0);
    lv_coord_t w = lv_obj_get_width(dd);
    if (w > 0) lv_obj_set_width(list, w);
}

static void col_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        style_dd_list(dd);
        return;
    }
    uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel >= (uint32_t)s_dd_n) return;
    fd_sync_col_t c;
    if (!find_col(s_dd_ids[sel], &c)) return;
    fd_dictnet_set_collection(c.id, c.name[0] ? c.name : c.id);
}

static void show_home(void)
{
    s_at_home = true;
    body_reset();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    label(s_body, _("DICTIONARY"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    label(s_body, _("LOOKUP_SAVE_TO"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    int sel = fill_dd();
    lv_obj_t *dd = lv_dropdown_create(s_body);
    lv_obj_set_width(dd, lv_pct(100));
    lv_obj_set_height(dd, 40);
    lv_obj_set_style_radius(dd, 8, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_color(dd, lv_color_white(), 0);
    lv_obj_set_style_text_font(dd, &fd_font_noto_14, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(dd, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_obj_set_style_pad_hor(dd, 10, 0);
    if (sel < 0) {
        lv_dropdown_set_options(dd, _("LOOKUP_NO_COLLECTION"));
        lv_obj_add_state(dd, LV_STATE_DISABLED);
    } else {
        lv_dropdown_set_options(dd, s_dd_opts);
        lv_dropdown_set_selected(dd, (uint32_t)sel);
        lv_obj_add_event_cb(dd, col_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(dd, col_dd_cb, LV_EVENT_READY, NULL);
        style_dd_list(dd);
    }
    uint32_t kids = lv_obj_get_child_count(dd);
    for (uint32_t i = 0; i < kids; i++) {
        lv_obj_t *ch = lv_obj_get_child(dd, i);
        if (lv_obj_check_type(ch, &lv_label_class)) {
            lv_obj_set_width(ch, lv_pct(88));
            lv_label_set_long_mode(ch, LV_LABEL_LONG_DOT);
        }
    }

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

    /* Kinh lup: tra tu dang nam trong o. Cham o nhap van mo ban phim. */
    lv_obj_t *tg = lv_button_create(row);
    lv_obj_set_size(tg, 40, 40);
    lv_obj_set_style_shadow_width(tg, 0, 0);
    lv_obj_set_style_radius(tg, 8, 0);
    lv_obj_set_style_pad_all(tg, 0, 0);
    lv_obj_set_style_bg_color(tg, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_color(tg, lv_color_hex(FD_COLOR_PRIMARY_DARK), LV_STATE_PRESSED);
    lv_obj_t *lens = lv_obj_create(tg);
    lv_obj_remove_style_all(lens);
    lv_obj_set_size(lens, 14, 14);
    lv_obj_set_pos(lens, 9, 8);
    lv_obj_set_scrollable(lens, false);
    lv_obj_remove_flag(lens, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(lens, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(lens, 2, 0);
    lv_obj_set_style_border_color(lens, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lens, LV_OPA_TRANSP, 0);
    lv_obj_t *handle = lv_obj_create(tg);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, 8, 2);
    lv_obj_set_pos(handle, 20, 22);
    lv_obj_set_scrollable(handle, false);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(handle, 1, 0);
    lv_obj_set_style_bg_color(handle, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(handle, 0, 0);
    lv_obj_set_style_transform_pivot_y(handle, 1, 0);
    lv_obj_set_style_transform_rotation(handle, 450, 0);
    lv_obj_add_event_cb(tg, search_cb, LV_EVENT_CLICKED, NULL);
    s_kb = fd_t9kb_create(s_body, s_ta, t9_done_cb, NULL);
    if (s_kb) fd_t9kb_set_qwerty_plain(s_kb);   /* chi chu thuong + xoa/cach/OK */
    lv_obj_add_event_cb(s_ta, ta_insert_cb, LV_EVENT_INSERT, NULL);
    lv_obj_add_event_cb(s_ta, kb_open_cb, LV_EVENT_CLICKED, NULL);
    kb_set_open(false);

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
            strlcpy(s_hwords[i], hist[i].en, sizeof(s_hwords[i]));
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
    /* 4.5.4: thoat Tu dien sau khi da luu tu -> nho fd_sync keo tu moi (gui lenh, khong chan UI). */
    if (fd_dictnet_take_added() > 0) fd_sync_check();
}

static bool on_back(void)
{
    if (s_gate) return false;
    if (!s_at_home) {
        show_home();
        return true;
    }
    return false;
}

const ui_screen_t g_scr_dict = {
    .name = "dict", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_back = on_back,
};
