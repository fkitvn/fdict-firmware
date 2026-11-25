/* Nhai lai: chon doan da dong bo, nghe Pico va cuon chu. Khong can mang de lay doan moi.
 * 5.4.0: neu goi tai ve (fd_feature_script("shadow")) co script Berry chay duoc va co init() thi man hinh do script
 * dieu khien (may ao fd_berry, API fd mo rong); khong thi (goi cu 1.0.0, script loi) dung man hinh C ben duoi. */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fd_berry.h"
#include "fd_display.h"
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
#define SND_KEY "sh_snd"
#define SPD_KEY "sh_spd"
#define HOLD_MS 3000

typedef enum { V_LIST, V_PLAY } view_t;

static view_t s_view;
static lv_obj_t *s_body, *s_viewp, *s_roll;
static lv_timer_t *s_tmr;
static EXT_RAM_BSS_ATTR fd_sync_pass_line_t s_lines[FD_SYNC_PASS_LINES];
static int s_n, s_est, s_pick, s_cur, s_focus_i, s_focus_n, s_nmark;
static int s_item0[FD_SYNC_PASS_LINES], s_itemn[FD_SYNC_PASS_LINES];
static fd_berry_roll_mark_t s_marks[FD_BERRY_MARK_MAX];
static bool s_vi, s_ipa, s_snd, s_silent, s_gate, s_focus_on;
static uint8_t s_spd = 1;
static bool s_arm, s_saw, s_heard, s_logged;
static int64_t s_t0;
static bool s_held, s_dragging;
static int64_t s_resume_at;
static lv_point_t s_press_pt;
static int32_t s_press_y;

static void show_list(void);
static void note_session(void);
static void apply_focus_bold(void);

/* 5.4.0 man hinh Berry */
static bool s_bmode;
static lv_obj_t *s_cv;
static lv_obj_t *s_bo[FD_BERRY_OBJ_MAX], *s_bl[FD_BERRY_OBJ_MAX];
static char s_berr[96];
static lv_timer_t *s_btmr;
static uint32_t s_rver, s_rseq, s_fseq;
static bool s_sess_open;
static char s_sess_mod[16];
static char *s_lbuf;
static const char *TAG = "shadow";

static int32_t i32abs(int32_t v) { return v < 0 ? -v : v; }

static void pause_reset(void)
{
    s_held = false;
    s_dragging = false;
    s_resume_at = 0;
}

static int spd_pct(void)
{
    switch (s_spd) {
    case 50: case 75: case 100: case 125: case 150: return s_spd;
    case 0: return 75;
    case 2: return 125;
    default: return 100;
    }
}

static const char *spd_label(void)
{
    switch (spd_pct()) {
    case 50: return "0.5x";
    case 75: return "0.75x";
    case 125: return "1.25x";
    case 150: return "1.5x";
    default: return "1x";
    }
}

static void spd_cycle(void)
{
    static const uint8_t seq[] = {50, 75, 100, 125, 150};
    int p = spd_pct(), i = 2;
    for (int k = 0; k < 5; k++) if (seq[k] == (uint8_t)p) { i = k; break; }
    s_spd = seq[(i + 1) % 5];
}

static void prefs_load(void)
{
    nvs_handle_t h;
    uint8_t vi = 1, ipa = 0, snd = 0, spd = 1;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, VI_KEY, &vi);
        nvs_get_u8(h, IPA_KEY, &ipa);
        nvs_get_u8(h, SND_KEY, &snd);
        nvs_get_u8(h, SPD_KEY, &spd);
        nvs_close(h);
    }
    s_vi = vi != 0;
    s_ipa = ipa != 0;
    s_snd = snd != 0;
    s_spd = spd;
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

static int line_est(int i)
{
    int n = (int)strlen(s_lines[i].text);
    int e = n * 90 * 100 / spd_pct();
    return e < 1500 ? 1500 : e;
}

static void note_session(void)
{
    if (!s_heard || s_logged) return;
    fd_sync_event_shadow();
    fd_sync_session_end();
    s_logged = true;
    s_arm = false;
}

static void apply_focus_bold(void)
{
    if (!s_roll) return;
    int n = (int)lv_obj_get_child_count(s_roll);
    for (int i = 0; i < n; i++) {
        lv_obj_t *l = lv_obj_get_child(s_roll, i);
        if (!l) continue;
        const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
        if (f != &fd_font_noto_18 && f != &fd_font_noto_18_bold) continue;
        bool on = s_focus_on && i == s_focus_i;
        lv_obj_set_style_text_font(l, on ? &fd_font_noto_18_bold : &fd_font_noto_18, 0);
    }
}

static void begin_sent(void)
{
    while (s_cur < s_n && !s_lines[s_cur].text[0]) s_cur++;
    if (s_cur >= s_n) return;
    s_est = line_est(s_cur);
    s_focus_on = true;
    s_focus_i = s_item0[s_cur];
    s_focus_n = s_itemn[s_cur] > 0 ? s_itemn[s_cur] : 1;
    apply_focus_bold();
    fd_audio_say_stop();
    if (!s_snd) {
        s_silent = true;
        s_arm = false;
        s_saw = true;
        s_t0 = esp_timer_get_time() / 1000;
        return;
    }
    s_silent = false;
    if (fd_audio_say_long_speed(s_lines[s_cur].text, spd_pct()) != ESP_OK) return;
    s_arm = true;
    s_saw = false;
}

static void advance_sent(void)
{
    if (s_cur + 1 < s_n) {
        s_cur++;
        begin_sent();
    } else {
        note_session();
    }
}

#define FOCUS_PAD 16

static bool focus_span(int32_t *y0, int32_t *y1)
{
    int n = (int)lv_obj_get_child_count(s_roll);
    int i0 = s_focus_i, i1 = s_focus_i + s_focus_n - 1;
    if (i0 < 0) i0 = 0;
    if (i1 >= n) i1 = n - 1;
    if (n <= 0 || i0 > i1) return false;
    lv_obj_t *a = lv_obj_get_child(s_roll, i0);
    lv_obj_t *b = lv_obj_get_child(s_roll, i1);
    if (!a || !b) return false;
    *y0 = lv_obj_get_y(a);
    *y1 = lv_obj_get_y(b) + lv_obj_get_height(b);
    return true;
}

static int32_t focus_target(int32_t vh, float p)
{
    int32_t y0, y1;
    if (!focus_span(&y0, &y1)) return lv_obj_get_y(s_roll);
    int32_t bh = y1 - y0;
    int32_t room = vh - FOCUS_PAD * 2;
    if (room < 1) room = 1;
    if (bh <= room) return FOCUS_PAD - y0;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    return FOCUS_PAD - y0 - (int32_t)(p * (float)(bh - room));
}

static bool mark_span(int k, int32_t *y0, int32_t *y1)
{
    if (k < 0 || k >= s_nmark || !s_roll) return false;
    int n = (int)lv_obj_get_child_count(s_roll);
    int i0 = s_marks[k].i, i1 = s_marks[k].i + s_marks[k].n - 1;
    if (i0 < 0) i0 = 0;
    if (i1 >= n) i1 = n - 1;
    if (n <= 0 || i0 > i1) return false;
    lv_obj_t *a = lv_obj_get_child(s_roll, i0);
    lv_obj_t *b = lv_obj_get_child(s_roll, i1);
    if (!a || !b) return false;
    *y0 = lv_obj_get_y(a);
    *y1 = lv_obj_get_y(b) + lv_obj_get_height(b);
    return true;
}

static int32_t keep_y(int k, int32_t vh)
{
    (void)vh;
    int32_t y0, y1;
    if (!mark_span(k, &y0, &y1)) return lv_obj_get_y(s_roll);
    return FOCUS_PAD - y0;
}

static int current_mark(void)
{
    for (int k = 0; k < s_nmark; k++) if (s_marks[k].i == s_focus_i) return k;
    return -1;
}

/* Cuon deu tu cau hien tai toi cau ke, khong nhay khi doi cau. */
static int32_t plan_target(int32_t vh, float p)
{
    if (s_nmark <= 0) return focus_target(vh, p);
    int k = current_mark();
    if (k < 0) return focus_target(vh, p);
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    int32_t y0 = keep_y(k, vh);
    int32_t y1;
    if (k + 1 < s_nmark) y1 = keep_y(k + 1, vh);
    else {
        int32_t a, b;
        if (!mark_span(k, &a, &b)) return y0;
        int32_t room = vh - FOCUS_PAD * 2;
        if (room < 1) room = 1;
        int32_t bh = b - a;
        y1 = bh > room ? FOCUS_PAD - a - (bh - room) : y0;
    }
    return y0 + (int32_t)((float)(y1 - y0) * p);
}

static void scroll_cb(lv_timer_t *t)
{
    (void)t;
    if ((!s_bmode && s_view != V_PLAY) || !s_viewp || !s_roll) return;
    if (lv_obj_is_hidden(s_viewp)) return;
    lv_obj_update_layout(s_viewp);
    int32_t vh = lv_obj_get_height(s_viewp);
    int32_t ch = lv_obj_get_height(s_roll);
    int32_t travel = vh + ch;
    if (travel < 1) travel = 1;
    bool busy = fd_audio_busy();
    int64_t now = esp_timer_get_time() / 1000;
    float p = 0;
    bool done = false;
    if (s_silent) {
        int64_t el = now - s_t0;
        if (el < 0) el = 0;
        p = s_est > 0 ? (float)el / (float)s_est : 1;
        if (p > 1) p = 1;
        if (!s_bmode && !s_heard) {
            fd_sync_session_begin("shadow");
            s_heard = true;
            s_logged = false;
        }
        if (!s_bmode && p >= 1 && !s_logged) done = true;
    } else {
        if (s_arm && busy && !s_saw) {
            s_saw = true;
            s_t0 = now;
            if (!s_bmode && !s_heard) {
                fd_sync_session_begin("shadow");
                s_heard = true;
                s_logged = false;
            }
        }
        if (s_saw) {
            int64_t el = now - s_t0;
            if (el < 0) el = 0;
            p = s_est > 0 ? (float)el / (float)s_est : 1;
            if (busy && p > 0.92f) p = 0.92f;
            if (!busy) p = 1;
            if (p < 0) p = 0;
            if (p > 1) p = 1;
        }
    }
    int32_t target = s_focus_on ? plan_target(vh, p) : (vh - (int32_t)(p * (float)travel));
    bool paused = s_held || (s_resume_at && now < s_resume_at);
    if (!paused) {
        int32_t cur = lv_obj_get_y(s_roll);
        int32_t d = target - cur;
        if (s_focus_on) {
            if (i32abs(d) <= 2) cur = target;
            else {
                int32_t step = (int32_t)((float)d * 0.4f);
                if (step == 0) step = d > 0 ? 1 : -1;
                if (step > 8) step = 8;
                if (step < -8) step = -8;
                cur += step;
            }
        } else if (i32abs(d) <= 3) cur = target;
        else {
            int32_t step = (int32_t)((float)d * 0.25f);
            if (step == 0) step = d > 0 ? 1 : -1;
            if (step > 24) step = 24;
            if (step < -24) step = -24;
            cur += step;
        }
        lv_obj_set_y(s_roll, cur);
    }
    if (s_bmode) fd_berry_roll_set_y((int)lv_obj_get_y(s_roll));
    else if (done) {
        if (s_focus_on) advance_sent();
        else note_session();
    } else if (!s_silent && s_arm && s_saw && !busy) {
        if (s_focus_on) advance_sent();
        else note_session();
    }
}

static void touch_cb(lv_event_t *e)
{
    if ((!s_bmode && s_view != V_PLAY) || !s_viewp || !s_roll) return;
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
    lv_obj_t *l = ui_label_create(parent, text, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(l, LV_OPA_40, 0);
    return l;
}

static void replay_cb(lv_event_t *e)
{
    (void)e;
    pause_reset();
    s_cur = 0;
    begin_sent();
}

static void show_play(void)
{
    s_n = fd_sync_passage_load(s_pick, s_lines, FD_SYNC_PASS_LINES);
    if (s_n <= 0) { show_list(); return; }
    s_heard = false;
    s_logged = false;
    s_arm = false;
    s_saw = false;
    s_silent = false;
    s_focus_on = false;
    s_cur = 0;
    pause_reset();
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
    int idx = 0;
    for (int i = 0; i < s_n; i++) {
        s_item0[i] = idx;
        s_itemn[i] = 1;
        lv_obj_t *t = ui_label_create(s_roll, s_lines[i].text, &fd_font_noto_18, FD_COLOR_TEXT);
        lv_obj_set_width(t, lv_pct(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        idx++;
        if (s_ipa && s_lines[i].ipa[0]) { muted(s_roll, s_lines[i].ipa); s_itemn[i]++; idx++; }
        if (s_vi && s_lines[i].vi[0]) { muted(s_roll, s_lines[i].vi); s_itemn[i]++; idx++; }
    }
    s_nmark = 0;
    for (int i = 0; i < s_n && s_nmark < FD_BERRY_MARK_MAX; i++) {
        s_marks[s_nmark].i = (int16_t)s_item0[i];
        s_marks[s_nmark].n = (int16_t)(s_itemn[i] > 0 ? s_itemn[i] : 1);
        s_marks[s_nmark].est = line_est(i);
        s_nmark++;
    }
    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *again = ui_gate_btn(row, _("SHADOW_AGAIN"), true, replay_cb);
    lv_obj_set_style_text_font(lv_obj_get_child(again, 0), &fd_font_noto_14, 0);
    s_tmr = lv_timer_create(scroll_cb, 50, NULL);
    begin_sent();
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
    else if (which == 2) { s_ipa = !s_ipa; pref_save(IPA_KEY, s_ipa ? 1 : 0); }
    else if (which == 3) { s_snd = !s_snd; pref_save(SND_KEY, s_snd ? 1 : 0); }
    else { spd_cycle(); pref_save(SPD_KEY, s_spd); }
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
    lv_obj_t *title = ui_label_create(s_body, _("SHADOW"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    int n = fd_sync_passage_count();
    if (n <= 0) {
        lv_obj_t *box = lv_obj_create(s_body);
        lv_obj_remove_style_all(box);
        lv_obj_set_width(box, lv_pct(100));
        lv_obj_set_flex_grow(box, 1);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *l = ui_label_create(box, _("SHADOW_NEED_MASTERED"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    lv_obj_t *opts = lv_obj_create(s_body);
    lv_obj_remove_style_all(opts);
    lv_obj_set_width(opts, lv_pct(100));
    lv_obj_set_height(opts, 36);
    lv_obj_set_flex_flow(opts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(opts, 4, 0);
    lv_obj_t *vi = ui_gate_btn(opts, _("SHADOW_MEANING"), s_vi, NULL);
    lv_obj_t *ipa = ui_gate_btn(opts, _("SHADOW_IPA"), s_ipa, NULL);
    lv_obj_t *snd = ui_gate_btn(opts, _("SHADOW_SOUND"), s_snd, NULL);
    lv_obj_t *spd = ui_gate_btn(opts, spd_label(), spd_pct() != 100, NULL);
    lv_obj_set_style_pad_hor(vi, 2, 0);
    lv_obj_set_style_pad_hor(ipa, 2, 0);
    lv_obj_set_style_pad_hor(snd, 2, 0);
    lv_obj_set_style_pad_hor(spd, 2, 0);
    lv_obj_add_event_cb(vi, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(ipa, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    lv_obj_add_event_cb(snd, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)3);
    lv_obj_add_event_cb(spd, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)4);
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

/* ---------- 5.4.0 man hinh Berry ---------- */
static const lv_font_t *bfont(uint8_t f)
{
    return f >= 32 ? &fd_font_mont_32 : f >= 24 ? &fd_font_mont_24 : f >= 18 ? &fd_font_noto_18 : f >= 14 ? &fd_font_noto_14 : &fd_font_noto_12;
}

static void btap_cb(lv_event_t *e)
{
    if (!s_bmode) return;
    fd_berry_event(FD_BERRY_EV_TAP, (int)(intptr_t)lv_event_get_user_data(e));
}

static void bapply(int i, const fd_berry_obj_t *o)
{
    if (!s_cv) return;
    if (!s_bo[i]) {
        lv_obj_t *b = lv_obj_create(s_cv);
        lv_obj_remove_style_all(b);
        lv_obj_set_scrollable(b, false);
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, btap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_clickable(l, false);
        s_bo[i] = b;
        s_bl[i] = l;
        memset(&g_ui_berry_cur[i], 0, sizeof(g_ui_berry_cur[i]));
        g_ui_berry_cur[i].font = 0xFF;
        int idx = s_viewp ? 1 : 0;   /* giu khung chu cuon o day, cac hop Berry noi phia tren */
        for (int j = 0; j < i; j++) if (s_bo[j]) idx++;
        lv_obj_move_to_index(b, idx);
    }
    lv_obj_t *b = s_bo[i], *l = s_bl[i];
    fd_berry_obj_t *c = &g_ui_berry_cur[i];
    bool first = c->font == 0xFF;
    lv_obj_set_hidden(b, !o->used || !o->vis);
    if (first || c->x != o->x || c->y != o->y) lv_obj_set_pos(b, o->x, o->y);
    if (first || c->opa != o->opa) lv_obj_set_style_opa(b, o->opa, 0);
    if (first || c->font != o->font) lv_obj_set_style_text_font(l, bfont(o->font), 0);
    if (first || c->w != o->w || c->h != o->h || c->align != o->align || c->font != o->font) {
        lv_obj_set_width(b, o->w > 0 ? o->w : LV_SIZE_CONTENT);
        lv_obj_set_height(b, o->h > 0 ? o->h : LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(l, o->align ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        if (o->w > 0) {
            lv_obj_set_width(l, lv_pct(100));
            if (o->align == 2) {
                lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
                lv_obj_set_height(l, lv_font_get_line_height(bfont(o->font)));
            } else {
                lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
                lv_obj_set_height(l, LV_SIZE_CONTENT);
            }
        } else {
            lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        }
        if (o->align) lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        else lv_obj_center(l);
    }
    if (first || c->bg != o->bg) {
        lv_obj_set_style_bg_opa(b, o->bg < 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        if (o->bg >= 0) lv_obj_set_style_bg_color(b, lv_color_hex((uint32_t)o->bg), 0);
    }
    if (first || c->bw != o->bw || c->bc != o->bc) {
        lv_obj_set_style_border_width(b, o->bw, 0);
        lv_obj_set_style_border_color(b, lv_color_hex((uint32_t)o->bc), 0);
    }
    if (first || c->radius != o->radius) lv_obj_set_style_radius(b, o->radius, 0);
    if (first || c->radius != o->radius || c->bg != o->bg) {
        bool pill = o->radius >= 16 && o->bg >= 0;
        lv_obj_set_style_shadow_width(b, pill ? 8 : 0, 0);
        lv_obj_set_style_shadow_opa(b, pill ? LV_OPA_10 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_offset_y(b, pill ? 2 : 0, 0);
        lv_obj_set_style_shadow_color(b, lv_color_hex(0x000000), 0);
    }
    if (first || c->pad != o->pad) lv_obj_set_style_pad_all(b, o->pad, 0);
    if (first || c->fg != o->fg) lv_obj_set_style_text_color(l, lv_color_hex((uint32_t)o->fg), 0);
    if (first || strcmp(c->txt, o->txt) != 0) lv_label_set_text(l, o->txt);
    *c = *o;
}

/* khung chu cuon: tao mot lan, dung lai nhan khi script doi noi dung */
static void broll_apply(const fd_berry_roll_t *r)
{
    if (!s_cv) return;
    if (!s_viewp) {
        s_viewp = lv_obj_create(s_cv);
        lv_obj_remove_style_all(s_viewp);
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
        lv_obj_move_to_index(s_viewp, 0);
        s_tmr = lv_timer_create(scroll_cb, 50, NULL);
    }
    bool on = r->h > 0 && r->w > 0;
    lv_obj_set_hidden(s_viewp, !on);
    lv_obj_set_scrollable(s_cv, !on);   /* khung chu dang hien: trang khong cuon, ve dau trang */
    if (on) {
        lv_obj_scroll_to_y(s_cv, 0, LV_ANIM_OFF);
        lv_obj_set_pos(s_viewp, r->x, r->y);
        lv_obj_set_size(s_viewp, r->w, r->h);
    }
    lv_obj_clean(s_roll);
    lv_obj_set_y(s_roll, 0);
    pause_reset();
    fd_berry_roll_item_t *it = heap_caps_malloc(sizeof(*it) * FD_BERRY_ROLL_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int n = it ? fd_berry_roll_items(it, FD_BERRY_ROLL_MAX) : 0;
    for (int i = 0; i < n; i++) {
        lv_obj_t *l = ui_label_create(s_roll, it[i].txt, bfont(it[i].font), (uint32_t)it[i].color);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        if (it[i].opa < 255) lv_obj_set_style_opa(l, it[i].opa, 0);
    }
    free(it);
    apply_focus_bold();
}

static void bcmd(const fd_berry_cmd_t *c, bool audio)
{
    switch (c->type) {
    case FD_BERRY_CMD_SAY_LONG:
        if (audio && s_lbuf && fd_berry_long_text(s_lbuf, FD_BERRY_LONG_MAX) > 0) {
            int spd = atoi(c->arg);
            if (spd < 50 || spd > 200) spd = 100;
            fd_audio_say_stop();
            fd_audio_say_long_speed(s_lbuf, spd);
        }
        break;
    case FD_BERRY_CMD_SAY_STOP: fd_audio_say_stop(); break;
    case FD_BERRY_CMD_SESS_BEGIN:
        fd_sync_session_begin(c->arg);
        strlcpy(s_sess_mod, c->arg, sizeof(s_sess_mod));
        s_sess_open = true;
        break;
    case FD_BERRY_CMD_EVENT:
        if (!strcmp(c->arg, "shadow")) fd_sync_event_shadow();
        break;
    case FD_BERRY_CMD_SESS_END:
        fd_sync_session_end();
        s_sess_open = false;
        break;
    default: break;
    }
}

/* Dung may ao, thuc hien lenh con lai (tru doc), ket thuc phien dang mo, xoa giao dien Berry. */
static void berry_teardown(void)
{
    if (!s_bmode) return;
    if (s_btmr) { lv_timer_delete(s_btmr); s_btmr = NULL; }
    fd_audio_say_stop();
    fd_berry_stop();
    fd_berry_cmd_t c;
    while (fd_berry_cmd_take(&c)) bcmd(&c, false);
    fd_audio_say_stop();
    if (s_sess_open) {
        if (!strcmp(s_sess_mod, "shadow")) fd_sync_event_shadow();
        fd_sync_session_end();
        s_sess_open = false;
    }
    fd_berry_pref_flush();
    if (s_tmr) { lv_timer_delete(s_tmr); s_tmr = NULL; }
    pause_reset();
    s_bmode = false;
    s_focus_on = false;
    s_fseq = 0;
    if (s_cv) lv_obj_delete(s_cv);
    s_cv = s_viewp = s_roll = NULL;
    memset(s_bo, 0, sizeof(s_bo));
    memset(s_bl, 0, sizeof(s_bl));
    free(s_lbuf);
    s_lbuf = NULL;
}

static void err_back_cb(lv_event_t *e) { (void)e; ui_nav_back(); }

static void show_berry_err(const char *err)
{
    fd_feature_mark_broken("shadow");
    berry_teardown();
    s_gate = false;
    s_bmode = false;
    s_view = V_LIST;
    if (!s_body) return;
    lv_obj_clean(s_body);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    lv_obj_t *t = ui_label_create(s_body, _("FEATURE_SCRIPT_ERR"), &fd_font_noto_18, FD_COLOR_ALERT);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    if (err && err[0]) {
        lv_obj_t *h = ui_label_create(s_body, err, &fd_font_noto_12, FD_COLOR_TEXT_MUTED);
        lv_obj_set_width(h, lv_pct(100));
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t *r = lv_obj_create(s_body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(70), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(r, false);
    ui_gate_btn(r, _("BACK"), true, err_back_cb);
}

static void berry_err_async(void *a)
{
    (void)a;
    if (!s_body || !s_bmode) return;
    show_berry_err(s_berr);
}

static void btick_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_bmode) return;
    fd_berry_misc_t m;
    uint32_t d = fd_berry_sync(g_ui_berry_tmp, &m);
    for (int i = 0; i < FD_BERRY_OBJ_MAX; i++)
        if (d & (1u << i)) bapply(i, &g_ui_berry_tmp[i]);
    fd_berry_cmd_t c;
    while (fd_berry_cmd_take(&c)) bcmd(&c, true);
    if (m.sound >= 0) fd_audio_feedback(m.sound == 1);
    fd_berry_roll_t r;
    fd_berry_roll_get(&r);
    s_nmark = fd_berry_roll_marks(s_marks, FD_BERRY_MARK_MAX);
    if (r.ver != s_rver) { s_rver = r.ver; broll_apply(&r); }
    if (r.run_seq != s_rseq) {
        s_rseq = r.run_seq;
        s_focus_on = false;
        s_est = r.est_ms < 5000 ? 5000 : (int)r.est_ms;
        s_silent = r.silent;
        pause_reset();
        if (s_viewp && s_roll) {
            lv_obj_update_layout(s_viewp);
            lv_obj_set_y(s_roll, lv_obj_get_height(s_viewp));
        }
        if (s_silent) {
            s_arm = false;
            s_saw = true;
            s_t0 = esp_timer_get_time() / 1000;
        } else {
            s_arm = true;
            s_saw = false;
        }
    }
    if (r.focus_seq != s_fseq) {
        bool first_focus = !s_focus_on;
        s_fseq = r.focus_seq;
        s_focus_on = r.focus_n > 0;
        s_focus_i = r.focus_i;
        s_focus_n = r.focus_n > 0 ? r.focus_n : 1;
        s_est = r.focus_est > 0 ? (int)r.focus_est : (r.est_ms > 0 ? (int)r.est_ms : 1500);
        s_silent = r.silent;
        pause_reset();
        if (s_silent) {
            s_arm = false;
            s_saw = true;
            s_t0 = esp_timer_get_time() / 1000;
        } else {
            s_arm = true;
            s_saw = false;
        }
        apply_focus_bold();
        /* Chi dat vi tri lan dau, cac cau sau de chu cuon deu toi moc ke. */
        if (first_focus && s_viewp && s_roll && s_focus_on) {
            lv_obj_update_layout(s_viewp);
            lv_obj_update_layout(s_roll);
            lv_obj_set_y(s_roll, plan_target(lv_obj_get_height(s_viewp), 0));
        }
    }
    fd_berry_set_busy(fd_audio_busy());
    fd_berry_pref_flush();
    if (m.state == FD_BERRY_ERROR || m.state == FD_BERRY_OVER) {
        const char *msg = m.state == FD_BERRY_ERROR ? m.err : "over";
        ESP_LOGW(TAG, "script dung (%s)", msg);
        strlcpy(s_berr, msg ? msg : "", sizeof(s_berr));
        lv_timer_pause(s_btmr);
        lv_async_call(berry_err_async, NULL);
    }
}

static bool bmeasure(const char *txt, int font, int maxw, int *w, int *h)
{
    if (!fd_display_lock(20)) return false;
    lv_point_t sz;
    lv_text_get_size(&sz, txt, bfont((uint8_t)font), 0, 0, maxw, LV_TEXT_FLAG_NONE);
    fd_display_unlock();
    *w = sz.x;
    *h = sz.y;
    return true;
}

static const char *h_tr(const char *k) { return _(k); }
static int h_pass_count(void) { return fd_sync_passage_count(); }
static bool h_pass_at(int i, char *out, size_t n) { return fd_sync_passage_at(i, out, n, NULL); }
static int h_pass_load(int i, fd_berry_line_t *out, int cap)
{
    fd_sync_pass_line_t *ls = heap_caps_malloc(sizeof(*ls) * FD_SYNC_PASS_LINES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ls) return 0;
    int n = fd_sync_passage_load(i, ls, FD_SYNC_PASS_LINES);
    if (n > cap) n = cap;
    for (int k = 0; k < n; k++) {
        strlcpy(out[k].text, ls[k].text, sizeof(out[k].text));
        strlcpy(out[k].vi, ls[k].vi, sizeof(out[k].vi));
        strlcpy(out[k].ipa, ls[k].ipa, sizeof(out[k].ipa));
    }
    free(ls);
    return n;
}

static bool berry_begin(void)
{
    size_t len = 0;
    char *sc = fd_feature_script("shadow", &len);
    if (!sc) return false;
    s_lbuf = heap_caps_malloc(FD_BERRY_LONG_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_lbuf) { free(sc); return false; }
    s_cv = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_cv);
    lv_obj_set_size(s_cv, lv_pct(100), lv_pct(100));
    lv_obj_set_scroll_dir(s_cv, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_cv, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_update_layout(s_cv);
    int32_t W = lv_obj_get_width(s_cv), H = lv_obj_get_height(s_cv);
    static const fd_berry_host_t HOST = {
        .tr = h_tr, .pass_count = h_pass_count, .pass_at = h_pass_at, .pass_load = h_pass_load,
    };
    static const char *const SIDE[] = {"top", "right", "bottom", "left"};
    ui_btn_side_t side = ui_btn_side();
    int si = side == UI_BTN_TOP ? 0 : side == UI_BTN_RIGHT ? 1 : side == UI_BTN_BOTTOM ? 2 : 3;
    fd_berry_cfg_t cfg = {
        .script = sc, .len = len, .words = NULL, .nwords = 0, .w = (int)W, .h = (int)H, .side = SIDE[si],
        .measure = bmeasure, .host = &HOST, .need_init = true,
    };
    memset(s_bo, 0, sizeof(s_bo));
    memset(s_bl, 0, sizeof(s_bl));
    s_rver = s_rseq = 0;
    s_sess_open = false;
    if (!fd_berry_start(&cfg)) {
        free(sc);
        lv_obj_delete(s_cv);
        s_cv = NULL;
        free(s_lbuf);
        s_lbuf = NULL;
        return false;
    }
    s_bmode = true;
    s_btmr = lv_timer_create(btick_cb, 30, NULL);
    return true;
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
    if (berry_begin()) return;
    ESP_LOGW(TAG, "berry_begin that bai");
    show_berry_err("init");
}

static void destroy(void)
{
    berry_teardown();
    fd_audio_say_stop();
    note_session();
    if (s_tmr) { lv_timer_delete(s_tmr); s_tmr = NULL; }
    pause_reset();
    s_body = s_viewp = s_roll = NULL;
}

static bool on_back(void)
{
    if (s_gate) return false;
    if (s_bmode) {   /* 5.4.0: script giu quay lai (dang nghe) thi cat tieng ngay roi goi back() */
        if (!fd_berry_hold_back()) return false;
        fd_audio_say_stop();
        fd_berry_event(FD_BERRY_EV_BACK, 0);
        return true;
    }
    if (s_view != V_LIST) { show_list(); return true; }
    return false;
}

const ui_screen_t g_scr_shadow = {
    .name = "shadow", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = NULL, .on_back = on_back,
};
