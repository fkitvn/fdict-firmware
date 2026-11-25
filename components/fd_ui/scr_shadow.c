/* Nhai lai: chon doan da dong bo, nghe Pico va cuon chu. Khong can mang de lay doan moi. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "fd_audio.h"
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "fd_feature.h"
#include "nvs.h"
#include "ui_priv.h"
#include "ui_swipe.h"

#define VI_KEY  "sh_vi"
#define IPA_KEY "sh_ipa"
#define HOLD_MS 3000

typedef enum { V_LIST, V_PLAY } view_t;

static view_t s_view;
static lv_obj_t *s_body, *s_viewp, *s_roll;
static lv_timer_t *s_tmr;
static fd_sync_pass_line_t s_lines[FD_SYNC_PASS_LINES];
static char s_say[2400];
static int s_n, s_est, s_pick;
static bool s_vi, s_ipa, s_gate;
static bool s_arm, s_saw, s_heard, s_logged;
static int64_t s_t0;
static bool s_held, s_dragging;
static int64_t s_resume_at;
static lv_point_t s_press_pt;
static int32_t s_press_y;

static void show_list(void);
static void note_session(void);

static int32_t i32abs(int32_t v) { return v < 0 ? -v : v; }

static void pause_reset(void)
{
    s_held = false;
    s_dragging = false;
    s_resume_at = 0;
}

static void prefs_load(void)
{
    nvs_handle_t h;
    uint8_t vi = 1, ipa = 0;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, VI_KEY, &vi);
        nvs_get_u8(h, IPA_KEY, &ipa);
        nvs_close(h);
    }
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
    pause_reset();
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
    int32_t target = vh - (int32_t)(p * (float)travel);
    bool paused = s_held || (s_resume_at && now < s_resume_at);
    if (!paused) {
        int32_t cur = lv_obj_get_y(s_roll);
        int32_t d = target - cur;
        if (i32abs(d) <= 3) cur = target;
        else {
            int32_t step = (int32_t)((float)d * 0.25f);
            if (step == 0) step = d > 0 ? 1 : -1;
            if (step > 24) step = 24;
            if (step < -24) step = -24;
            cur += step;
        }
        lv_obj_set_y(s_roll, cur);
    }
    if (s_arm && s_saw && !busy) note_session();
}

static void touch_cb(lv_event_t *e)
{
    if (s_view != V_PLAY || !s_viewp || !s_roll) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_event_get_indev(e);
    lv_point_t p = {0, 0};
    if (in) lv_indev_get_point(in, &p);
    int32_t dw = lv_display_get_horizontal_resolution(NULL);
    if (code == LV_EVENT_PRESSED) {
        /* Vuot mep de quay lai: khong bat giu chu, de cu chi canh van chay. */
        if (p.x < FD_SWIPE_EDGE_PX || p.x >= dw - FD_SWIPE_EDGE_PX) return;
        s_dragging = false;
        s_press_pt = p;
        s_press_y = lv_obj_get_y(s_roll);
        s_held = true;
        s_resume_at = 0;
    } else if (code == LV_EVENT_PRESSING && s_held) {
        int32_t dy = p.y - s_press_pt.y;
        int32_t dx = p.x - s_press_pt.x;
        if (i32abs(dy) > i32abs(dx) && i32abs(dy) > 4) s_dragging = true;
        if (s_dragging) {
            lv_obj_update_layout(s_viewp);
            int32_t vh = lv_obj_get_height(s_viewp);
            int32_t ch = lv_obj_get_height(s_roll);
            int32_t y = s_press_y + dy;
            if (y > vh) y = vh;
            if (y < -ch) y = -ch;
            lv_obj_set_y(s_roll, y);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_held) {
            s_held = false;
            s_dragging = false;
            s_resume_at = esp_timer_get_time() / 1000 + HOLD_MS;
        }
    }
}

static lv_obj_t *muted(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = ui_label_create(parent, text, &fd_font_noto_12, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(l, LV_OPA_40, 0);
    return l;
}

static void replay_cb(lv_event_t *e) { (void)e; pause_reset(); begin_audio(); }

static void show_play(void)
{
    s_n = fd_sync_passage_load(s_pick, s_lines, FD_SYNC_PASS_LINES);
    if (s_n <= 0) { show_list(); return; }
    s_heard = false;
    s_logged = false;
    s_arm = false;
    s_saw = false;
    pause_reset();
    join_say();
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
    lv_obj_set_clickable(s_viewp, true);
    lv_obj_add_event_cb(s_viewp, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_viewp, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_viewp, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_viewp, touch_cb, LV_EVENT_PRESS_LOST, NULL);
    s_roll = lv_obj_create(s_viewp);
    lv_obj_remove_style_all(s_roll);
    lv_obj_set_width(s_roll, lv_pct(100));
    lv_obj_set_height(s_roll, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_roll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_roll, 10, 0);
    lv_obj_set_clickable(s_roll, true);
    lv_obj_add_event_cb(s_roll, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_roll, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_roll, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_roll, touch_cb, LV_EVENT_PRESS_LOST, NULL);
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
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *again = ui_gate_btn(row, _("SHADOW_AGAIN"), true, replay_cb);
    lv_obj_set_style_text_font(lv_obj_get_child(again, 0), &fd_font_noto_14, 0);
    s_tmr = lv_timer_create(scroll_cb, 50, NULL);
    begin_audio();
}

static void play_cb(lv_event_t *e)
{
    s_pick = (int)(intptr_t)lv_event_get_user_data(e);
    show_play();
}

static void toggle_cb(lv_event_t *e)
{
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    if (which == 1) { s_vi = !s_vi; pref_save(VI_KEY, s_vi ? 1 : 0); }
    else { s_ipa = !s_ipa; pref_save(IPA_KEY, s_ipa ? 1 : 0); }
    show_list();
}

static void show_list(void)
{
    fd_audio_say_stop();
    note_session();
    s_view = V_LIST;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_t *title = ui_label_create(s_body, _("SHADOW"), &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    int n = fd_sync_passage_count();
    if (n <= 0) {
        lv_obj_t *l = ui_label_create(s_body, _("SHADOW_NEED_MASTERED"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        return;
    }
    lv_obj_t *opts = lv_obj_create(s_body);
    lv_obj_remove_style_all(opts);
    lv_obj_set_width(opts, lv_pct(100));
    lv_obj_set_height(opts, 36);
    lv_obj_set_flex_flow(opts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(opts, 8, 0);
    lv_obj_t *vi = ui_gate_btn(opts, _("SHADOW_MEANING"), s_vi, NULL);
    lv_obj_t *ipa = ui_gate_btn(opts, _("SHADOW_IPA"), s_ipa, NULL);
    lv_obj_add_event_cb(vi, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(ipa, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    lv_obj_t *list = lv_obj_create(s_body);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scrollable(list, true);
    for (int i = 0; i < n; i++) {
        char preview[72];
        if (!fd_sync_passage_at(i, preview, sizeof(preview), NULL)) continue;
        lv_obj_t *b = lv_button_create(list);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_add_event_cb(b, play_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lab = ui_label_create(b, preview, &fd_font_noto_14, FD_COLOR_TEXT);
        lv_obj_set_width(lab, lv_pct(100));
        lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    }
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    if (!fd_feature_local("shadow", NULL, 0)) {
        s_gate = true;
        ui_label_create(s_body, _("FEATURE_WIFI"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
        return;
    }
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    prefs_load();
    show_list();
}

static void destroy(void)
{
    fd_audio_say_stop();
    note_session();
    if (s_tmr) { lv_timer_delete(s_tmr); s_tmr = NULL; }
    pause_reset();
    s_body = s_viewp = s_roll = NULL;
}

static bool on_back(void)
{
    if (s_gate) return false;
    if (s_view != V_LIST) { show_list(); return true; }
    return false;
}

const ui_screen_t g_scr_shadow = {
    .name = "shadow", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = NULL, .on_back = on_back,
};
