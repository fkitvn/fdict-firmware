/* Nhai: hoc online. Chon bo tu, do dai, nghia va IPA. Pico doc, chu cuon tu duoi len.
 * Chi ghi mot su kien lich su phien. Khong cham diem, khong doi tu da thuoc. */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "fd_audio.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_shadow.h"
#include "fd_sync.h"
#include "nvs.h"
#include "ui_priv.h"

#define LEN_KEY "sh_len"
#define VI_KEY  "sh_vi"
#define IPA_KEY "sh_ipa"

typedef enum { V_SETUP, V_LOAD, V_MSG, V_PLAY } view_t;

static view_t s_view;
static lv_obj_t *s_body, *s_viewp, *s_roll;
static lv_timer_t *s_tmr;
static fd_shadow_line_t s_lines[FD_SHADOW_LINES];
static char s_say[1400];
static char s_msg[96];
static int s_n, s_len, s_est;
static bool s_vi, s_ipa, s_gate, s_wifi;
static bool s_arm, s_saw, s_heard, s_logged;
static int64_t s_t0;
static fd_shadow_status_t s_net;
static void wifi_cb(lv_event_t *e);
static void back_setup_cb(lv_event_t *e);

static const char *const LEN_NAME[] = {"short", "medium", "long"};
static const char *const LEN_KEY_I18N[] = {"SHADOW_SHORT", "SHADOW_MED", "SHADOW_LONG"};

static void show_setup(void);
static void show_play(void);
static void note_session(void);

static void prefs_load(void)
{
    nvs_handle_t h;
    uint8_t len = 0, vi = 1, ipa = 0;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, LEN_KEY, &len);
        nvs_get_u8(h, VI_KEY, &vi);
        nvs_get_u8(h, IPA_KEY, &ipa);
        nvs_close(h);
    }
    s_len = len > 2 ? 0 : len;
    s_vi = vi != 0;
    s_ipa = ipa != 0;
}

static void pref_save(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

static void body_clear(void)
{
    if (s_tmr) { lv_timer_delete(s_tmr); s_tmr = NULL; }
    s_viewp = s_roll = NULL;
    if (s_body) lv_obj_clean(s_body);
}

static void join_say(void)
{
    size_t n = 0;
    s_say[0] = 0;
    for (int i = 0; i < s_n; i++) {
        size_t m = strlen(s_lines[i].text);
        if (!m) continue;
        if (n && n + 1 < sizeof(s_say)) s_say[n++] = ' ';
        if (n + m >= sizeof(s_say)) m = sizeof(s_say) - 1 - n;
        if (!m) break;
        memcpy(s_say + n, s_lines[i].text, m);
        n += m;
        s_say[n] = 0;
    }
    int est = (int)n * 90;
    s_est = est < 8000 ? 8000 : est;
}

static void note_session(void)
{
    if (!s_heard || s_logged) return;
    fd_sync_event_shadow();
    fd_sync_session_end();
    s_logged = true;
    s_arm = false;
}

static void begin_audio(void)
{
    if (!s_say[0]) return;
    fd_audio_say_stop();
    if (fd_audio_say_long(s_say) != ESP_OK) return;
    s_arm = true;
    s_saw = false;
}

static void scroll_cb(lv_timer_t *t)
{
    (void)t;
    if (s_view != V_PLAY || !s_viewp || !s_roll) return;
    lv_obj_update_layout(s_viewp);
    int32_t vh = lv_obj_get_height(s_viewp);
    int32_t ch = lv_obj_get_height(s_roll);
    int32_t travel = vh + ch;
    if (travel < 1) travel = 1;
    bool busy = fd_audio_busy();
    int64_t now = esp_timer_get_time() / 1000;
    if (s_arm && busy && !s_saw) {
        s_saw = true;
        s_t0 = now;
        if (!s_heard) {
            fd_sync_session_begin("shadow");
            s_heard = true;
            s_logged = false;
        }
    }
    float p = 0;
    if (s_saw) {
        int64_t el = now - s_t0;
        if (el < 0) el = 0;
        p = s_est > 0 ? (float)el / (float)s_est : 1;
        if (busy && p > 0.92f) p = 0.92f;
        if (!busy) p = 1;
        if (p < 0) p = 0;
        if (p > 1) p = 1;
    }
    lv_obj_set_y(s_roll, vh - (int32_t)(p * (float)travel));
    if (s_arm && s_saw && !busy) note_session();
}

static lv_obj_t *muted(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = ui_label_create(parent, text, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(l, LV_OPA_40, 0);
    return l;
}

static void replay_cb(lv_event_t *e) { (void)e; begin_audio(); }
static void next_cb(lv_event_t *e);

static void show_play(void)
{
    s_view = V_PLAY;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    s_viewp = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_viewp);
    lv_obj_set_width(s_viewp, lv_pct(100));
    lv_obj_set_flex_grow(s_viewp, 1);
    lv_obj_set_scrollable(s_viewp, false);
    s_roll = lv_obj_create(s_viewp);
    lv_obj_remove_style_all(s_roll);
    lv_obj_set_width(s_roll, lv_pct(100));
    lv_obj_set_height(s_roll, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_roll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_roll, 10, 0);
    for (int i = 0; i < s_n; i++) {
        lv_obj_t *t = ui_label_create(s_roll, s_lines[i].text, &fd_font_noto_18, FD_COLOR_TEXT);
        lv_obj_set_width(t, lv_pct(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        if (s_ipa && s_lines[i].ipa[0]) muted(s_roll, s_lines[i].ipa);
        if (s_vi && s_lines[i].vi[0]) muted(s_roll, s_lines[i].vi);
    }
    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *again = ui_gate_btn(row, _("SHADOW_AGAIN"), false, replay_cb);
    lv_obj_t *next = ui_gate_btn(row, _("SHADOW_NEXT"), true, next_cb);
    lv_obj_set_style_text_font(lv_obj_get_child(again, 0), &fd_font_noto_14, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(next, 0), &fd_font_noto_14, 0);
    s_tmr = lv_timer_create(scroll_cb, 50, NULL);
    begin_audio();
}

static void show_msg(void)
{
    note_session();
    fd_audio_say_stop();
    s_view = V_MSG;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    lv_obj_t *l = ui_label_create(s_body, s_msg[0] ? s_msg : _("SHADOW_FAIL"), &fd_font_noto_14, FD_COLOR_TEXT);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *b = s_wifi ? ui_gate_btn(row, _("OPEN_WIFI"), true, wifi_cb) : NULL;
    lv_obj_t *back = ui_gate_btn(row, _("BACK"), !s_wifi, back_setup_cb);
    if (b) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &fd_font_noto_14, 0);
    if (back) lv_obj_set_style_text_font(lv_obj_get_child(back, 0), &fd_font_noto_14, 0);
}

static void wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }
static void back_setup_cb(lv_event_t *e) { (void)e; show_setup(); }

static void show_load(void)
{
    s_view = V_LOAD;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label_create(s_body, _("SHADOW_LOADING"), &fd_font_noto_18, FD_COLOR_TEXT_MUTED);
}

static void take_lines(const fd_shadow_status_t *st)
{
    s_n = st->n;
    if (s_n > FD_SHADOW_LINES) s_n = FD_SHADOW_LINES;
    memset(s_lines, 0, sizeof(s_lines));
    if (s_n > 0) memcpy(s_lines, st->line, (size_t)s_n * sizeof(s_lines[0]));
    s_heard = false;
    s_logged = false;
    s_arm = false;
    s_saw = false;
    join_say();
}

static void poll_load(void)
{
    if (s_view != V_LOAD) return;
    fd_shadow_get(&s_net);
    if (s_net.state == FD_SHADOW_LOADING || s_net.state == FD_SHADOW_IDLE) return;
    if (s_net.state == FD_SHADOW_OK && s_net.n > 0) {
        take_lines(&s_net);
        show_play();
        return;
    }
    s_wifi = s_net.http < 0;
    if (s_net.message[0]) snprintf(s_msg, sizeof(s_msg), "%s", s_net.message);
    else if (s_net.empty) snprintf(s_msg, sizeof(s_msg), "%s", _("SHADOW_NEED_MASTERED"));
    else if (s_wifi) snprintf(s_msg, sizeof(s_msg), "%s", _("SHADOW_NEED_WIFI"));
    else s_msg[0] = 0;
    show_msg();
}

static void start(void)
{
    if (!fd_net_is_connected()) {
        s_wifi = true;
        snprintf(s_msg, sizeof(s_msg), "%s", _("SHADOW_NEED_WIFI"));
        show_msg();
        return;
    }
    fd_sync_pick_t pick;
    fd_sync_pick_get(&pick);
    if (pick.mode == FD_SYNC_PICK_NEW) {
        s_wifi = false;
        snprintf(s_msg, sizeof(s_msg), "%s", _("SHADOW_NEW_DECK"));
        show_msg();
        return;
    }
    char ids[FD_SYNC_MAX_COLS][FD_SYNC_WID_LEN];
    int nids = 0;
    if (pick.mode == FD_SYNC_PICK_COMBINED) {
        nids = fd_sync_pick_sel(ids, FD_SYNC_MAX_COLS);
        if (nids <= 0) { fd_ui_toast(_("NO_DECK_WORDS")); return; }
    }
    note_session();
    fd_audio_say_stop();
    s_heard = false;
    s_logged = false;
    if (!fd_shadow_start(LEN_NAME[s_len], nids ? ids : NULL, nids)) {
        s_wifi = true;
        snprintf(s_msg, sizeof(s_msg), "%s", _("SHADOW_NEED_WIFI"));
        show_msg();
        return;
    }
    show_load();
}

static void next_cb(lv_event_t *e) { (void)e; start(); }

static void setup_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_setup(); }

static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4);
    int v = (int)(ud & 0xF);
    if (grp == 0 && v >= 0 && v <= 2) { s_len = v; pref_save(LEN_KEY, (uint8_t)v); }
    else if (grp == 1) { s_vi = v == 1; pref_save(VI_KEY, s_vi ? 1 : 0); }
    else if (grp == 2) { s_ipa = v == 1; pref_save(IPA_KEY, s_ipa ? 1 : 0); }
    lv_async_call(setup_async, NULL);
}

static void begin_cb(lv_event_t *e) { (void)e; start(); }

static void show_setup(void)
{
    note_session();
    fd_audio_say_stop();
    s_view = V_SETUP;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sc = ui_setup_begin(s_body, _("SHADOW"));
    ui_setup_deck(sc, setup_async);
    ui_setup_opt_row(sc, _("SHADOW_LEN"), LEN_KEY_I18N, 3, s_len, opt_cb, 0, 0);
    static const char *const SW[] = {"OFF", "ON"};
    ui_setup_opt_row(sc, _("SHADOW_MEANING"), SW, 2, s_vi ? 1 : 0, opt_cb, 1, 0);
    ui_setup_opt_row(sc, _("SHADOW_IPA"), SW, 2, s_ipa ? 1 : 0, opt_cb, 2, 0);
    ui_setup_start(s_body, begin_cb);
}

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
    fd_shadow_cancel();
    fd_audio_say_stop();
    note_session();
    if (s_tmr) { lv_timer_delete(s_tmr); s_tmr = NULL; }
    s_body = s_viewp = s_roll = NULL;
}

static void on_tick(void) { if (!s_gate) poll_load(); }

static bool on_back(void)
{
    if (s_gate) return false;
    if (s_view != V_SETUP) { show_setup(); return true; }
    return false;
}

const ui_screen_t g_scr_shadow = {
    .name = "shadow", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = on_tick, .on_back = on_back,
};
