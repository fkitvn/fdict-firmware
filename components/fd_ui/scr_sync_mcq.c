/* 2.3.5: Phan loai tu + Dien tu — trac nghiem 4 dap an offline tu kho dong bo. */
#include "ui_priv.h"
#include "fd_audio.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_i18n.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "lvgl.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

typedef enum { MCQ_POS = 0, MCQ_CLOZE = 1 } mcq_kind_t;
typedef enum { V_SETUP = 0, V_QUIZ, V_RESULT, V_MSG } view_t;

static mcq_kind_t s_kind;
static view_t s_view;
static lv_obj_t *s_body, *s_dlg;
static lv_obj_t *s_ans[4], *s_lbl[4];
static lv_timer_t *s_next_t;
static fd_study_session_t *s_sess;
static int s_pos, s_score, s_done, s_ncfg;
static bool s_answered, s_gate;
static const int NOPT[3] = {10, 20, 0};
static uint8_t s_reps;    /* 2.3.6: tu dong phat nhu Quiz: 0 Tat (mac dinh) / 1 / 2 lan; NVS fd/pos_rep | fd/clz_rep */
static int s_said_pos = -1;
static const char *rep_key(void);
static void reps_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, rep_key(), &v); nvs_close(h); }
    s_reps = (v == 1 || v == 2) ? 1 : 0;   /* Bat/Tat: gia tri cu 1/2 = Bat */
}
static void reps_save(void)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, rep_key(), s_reps);
    nvs_commit(h);
    nvs_close(h);
}
static void mcq_say(const char *t)
{
    if (!s_reps || !t || !t[0]) return;
    const fd_pron_cfg_t c = {FD_PRON_PICO_ONLY, s_reps, false};   /* nhu Quiz */
    ui_pron_say_cfg(t, &c);
}

static void show_setup(void);
static void show_quiz(void);
static void show_result(void);
static void back_setup_cb(lv_event_t *e);

static void stop_timer(void)
{
    if (s_next_t) { lv_timer_delete(s_next_t); s_next_t = NULL; }
}

static void body_reset(void)
{
    stop_timer();
    s_dlg = NULL;
    for (int i = 0; i < 4; i++) s_ans[i] = s_lbl[i] = NULL;
    if (s_body) lv_obj_clean(s_body);
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, t ? t : "");
    return l;
}

static lv_obj_t *hrow(lv_obj_t *p, int32_t h)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    return r;
}

static lv_obj_t *btn(lv_obj_t *p, const char *t, bool primary, lv_event_cb_t cb, intptr_t ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(primary ? 0xFFFFFF : FD_COLOR_TEXT), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)ud);
    return b;
}

static const char *title_key(void) { return s_kind == MCQ_POS ? "WORD_TYPE" : "FILL_BLANK"; }
static const char *ev_kind(void) { return s_kind == MCQ_POS ? "pos" : "cloze"; }
static const char *rep_key(void) { return s_kind == MCQ_POS ? "pos_rep" : "clz_rep"; }

enum { OPT_N, OPT_REP };
static void setup_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_setup(); }
static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_N) s_ncfg = v;
    else if (grp == OPT_REP) { s_reps = (uint8_t)v; reps_save(); }
    lv_async_call(setup_async, NULL);   /* dung lai man ngoai callback nut */
}

static bool build_session(void)
{
    if (!s_sess) s_sess = calloc(1, sizeof(*s_sess));
    if (!s_sess) return false;
    int n = NOPT[s_ncfg] ? NOPT[s_ncfg] : 10;
    fd_study_err_t err = (s_kind == MCQ_POS)
        ? fd_sync_build_pos_quiz(s_sess, n)
        : fd_sync_build_cloze_quiz(s_sess, n);
    return err == FD_STUDY_ERR_NONE && s_sess->n > 0;
}

static void show_msg_no_data(void)
{
    s_view = V_MSG;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    label(s_body, _("NO_OFFLINE_DATA_YET"), &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_t *r = hrow(s_body, 44);
    lv_obj_set_style_pad_column(r, 8, 0);
    btn(r, _("BACK"), true, back_setup_cb, 0);
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    if (fd_sync_pick_count(0) <= 0) { fd_ui_toast(_("NO_DECK_WORDS")); return; }
    if (!fd_sync_ready() || !build_session()) { show_msg_no_data(); return; }
    s_pos = s_score = s_done = 0;
    s_said_pos = -1;
    s_answered = false;
    show_quiz();
}

static void back_setup_cb(lv_event_t *e) { (void)e; show_setup(); }
static void home_cb(lv_event_t *e) { (void)e; ui_nav_home(); }
static void again_cb(lv_event_t *e) { start_cb(e); }

static void show_setup(void)
{
    s_view = V_SETUP;
    body_reset();
    lv_obj_t *sc = ui_setup_begin(s_body, _(title_key()));   /* 2.3.6: mau cai dat chung (nhu Quiz) */
    ui_setup_deck(sc, setup_async);
    static const char *const NS[] = {"10", "20", "ENDLESS"};
    static const char *const RP[] = {"OFF", "ON"};
    ui_setup_opt_row(sc, _("QUESTIONS"), NS, 3, s_ncfg, opt_cb, OPT_N, 0);
    ui_setup_opt_row(sc, _("AUTO_PLAY"), RP, 2, s_reps, opt_cb, OPT_REP, UI_OPT_WIDE);
    ui_setup_start(s_body, start_cb);
}

static void show_result(void)
{
    s_view = V_RESULT;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    label(s_body, _(title_key()), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    char buf[48];
    snprintf(buf, sizeof(buf), "%d / %d", s_score, s_done);
    label(s_body, buf, &fd_font_mont_32, FD_COLOR_TEXT);
    lv_obj_t *r = hrow(s_body, 44);
    lv_obj_set_style_pad_column(r, 8, 0);
    btn(r, _("HOME"), false, home_cb, 0);
    btn(r, _("RETRY"), true, again_cb, 0);
}

static void next_cb(lv_timer_t *t)
{
    (void)t;
    s_next_t = NULL;
    if (!s_sess) return;
    if (++s_pos >= s_sess->n) show_result();
    else show_quiz();
}

static void answer(int i)
{
    if (s_answered || s_view != V_QUIZ || !s_sess || s_pos >= s_sess->n) return;
    const fd_study_q_t *q = &s_sess->q[s_pos];
    s_answered = true;
    int c = q->answer;
    s_done++;
    if (i == c) s_score++;
    for (int k = 0; k < 4; k++) {
        if (!s_ans[k]) continue;
        ui_abcd_mark(s_ans[k], s_lbl[k], k == c ? UI_ANS_CORRECT : (k == i ? UI_ANS_WRONG : UI_ANS_OTHER));
    }
    fd_audio_feedback(i == c);
    if (s_kind == MCQ_CLOZE && c >= 0 && c < 4) mcq_say(q->choice[c]);   /* Dien tu: doc tu dung SAU khi tra loi */
    fd_sync_event(ev_kind(), q->id, i == c);
    s_next_t = lv_timer_create(next_cb, i == c ? 900 : 1500, NULL);
    lv_timer_set_repeat_count(s_next_t, 1);
}

static void ans_cb(lv_event_t *e) { answer((int)(intptr_t)lv_event_get_user_data(e)); }

static void show_quiz(void)
{
    if (!s_sess || s_pos >= s_sess->n) { show_result(); return; }
    const fd_study_q_t *q = &s_sess->q[s_pos];
    s_view = V_QUIZ;
    body_reset();
    s_answered = false;
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 4, 0);

    lv_obj_t *top = hrow(s_body, 24);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pg = label(top, "", NULL, FD_COLOR_TEXT_MUTED);
    lv_label_set_text_fmt(pg, _("QUIZ_QUESTION_N_OF"), s_pos + 1, s_sess->n);
    lv_obj_t *sc = label(top, "", NULL, FD_COLOR_PRIMARY_TEXT);
    lv_label_set_text_fmt(sc, _("QUIZ_SCORE"), s_score);

    lv_obj_t *pr = lv_obj_create(s_body);
    lv_obj_remove_style_all(pr);
    lv_obj_set_width(pr, lv_pct(100));
    lv_obj_set_flex_grow(pr, 1);
    lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pl = label(pr, q->prompt, &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_set_width(pl, lv_pct(100));
    lv_obj_set_style_text_align(pl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(pl, LV_LABEL_LONG_WRAP);
    if (s_kind == MCQ_POS && q->ipa[0]) label(pr, q->ipa, NULL, FD_COLOR_PRIMARY_TEXT);
    if (s_kind == MCQ_POS && s_said_pos != s_pos) { s_said_pos = s_pos; mcq_say(q->prompt); }   /* Phan loai tu: doc tu de bai */

    bool por = (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL));
    lv_obj_t *g = lv_obj_create(s_body);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, lv_pct(100), por ? 2 * 64 + 4 : 2 * 48 + 6);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(g, por ? 4 : 6, 0);
    lv_obj_set_style_pad_column(g, 6, 0);
    lv_obj_update_layout(s_body);
    int32_t cw = (lv_obj_get_content_width(g) - 6) / 2;
    if (cw < 40) cw = 40;
    for (int cell = 0; cell < 4; cell++) {
        lv_obj_t *l;
        lv_obj_t *b = ui_abcd_cell(g, cell, q->choice[cell], &l);   /* 4.1.9: giong Luyen tap - A/B/C/D, mot mau */
        lv_obj_set_size(b, cw, por ? 64 : 48);
        if (por && l) lv_obj_set_height(l, 60);
        if (l) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        s_ans[cell] = b;
        s_lbl[cell] = l;
        lv_obj_add_event_cb(b, ans_cb, LV_EVENT_CLICKED, (void *)(intptr_t)cell);
    }
}

static void dlg_close(void) { if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; } }

static void dlg_cb(lv_event_t *e)
{
    bool yes = (bool)(intptr_t)lv_event_get_user_data(e);
    dlg_close();
    if (yes) {
        if (s_done) show_result();
        else show_setup();
    }
}

static void dlg_open(void)
{
    if (s_dlg) return;
    s_dlg = lv_obj_create(lv_obj_get_parent(s_body));
    lv_obj_set_size(s_dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_50, 0);
    lv_obj_t *box = lv_obj_create(s_dlg);
    lv_obj_set_size(box, lv_pct(80), LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    label(box, _("QUIT_THIS_QUIZ"), &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_t *r = hrow(box, 40);
    lv_obj_set_style_pad_column(r, 8, 0);
    btn(r, _("CANCEL"), false, dlg_cb, 0);
    btn(r, _("QUIT"), true, dlg_cb, 1);
}

static void build_common(lv_obj_t *scr, mcq_kind_t kind)
{
    s_kind = kind;
    reps_load();
    s_body = ui_page_create(scr);
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    show_setup();
}

static void destroy_common(void)
{
    stop_timer();
    s_body = s_dlg = NULL;
    free(s_sess); s_sess = NULL;
}

static bool on_back_common(void)
{
    if (s_dlg) { dlg_close(); return true; }
    if (s_view == V_QUIZ) { dlg_open(); return true; }
    if (s_view == V_RESULT || s_view == V_MSG) { show_setup(); return true; }
    return false;
}

static bool on_button_common(const fd_evt_button_t *b)
{
    if (!b || s_gate || s_view != V_QUIZ || s_answered) return false;
    if (b->action != FD_BTN_CLICK) return false;
    int cell = ui_abcd_button(b->index);   /* nut 1..4 -> A..D, khong phu thuoc xoay */
    if (cell < 0) return false;
    answer(cell);
    return true;
}

static void build_pos(lv_obj_t *scr) { build_common(scr, MCQ_POS); }
static void build_cloze(lv_obj_t *scr) { build_common(scr, MCQ_CLOZE); }
static void destroy(void) { destroy_common(); }
static bool on_back(void) { return on_back_common(); }
static bool on_button(const fd_evt_button_t *b) { return on_button_common(b); }

const ui_screen_t g_scr_pos = {
    .name = "pos", .parent = FD_SCR_HOME, .build = build_pos, .destroy = destroy, .on_back = on_back, .on_button = on_button,
};
const ui_screen_t g_scr_cloze = {
    .name = "cloze", .parent = FD_SCR_HOME, .build = build_cloze, .destroy = destroy, .on_back = on_back, .on_button = on_button,
};
