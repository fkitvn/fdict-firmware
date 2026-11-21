/* 4.8.0 Tro choi (Games) - tro choi dau tien: Hung tu (Word catch).
 * Mot the tu (nghia tieng Viet hoac tu tieng Anh) chay ve phia cum nut; hai cong nam sat cum nut, moi cong ghi mot dap an.
 * Chon cong dung truoc khi the cham cong. Dung lien tiep = nhan diem (toi da x5), moi `every` cau dung thi len cap va the
 * chay nhanh hon (ramp_pct), sai hoac de the cham cong = mat 1 mang. Het mang = ket thuc, luu ky luc (NVS).
 *
 * Nut theo goc nhin nguoi dung (ui_btn_user_pos, dung moi huong xoay):
 *   180 do (nut DUOI) the roi xuong, cong trai | phai o day man.   Nut 1 2 = cong trai, Nut 3 4 = cong phai.
 *   0 do   (nut TREN) the bay len,  cong trai | phai o dinh man.   Nut 1 2 = cong trai, Nut 3 4 = cong phai.
 *   90 do  (nut PHAI) the chay sang phai, cong tren | duoi ben phai. Nut 1 2 = cong tren, Nut 3 4 = cong duoi.
 *   270 do (nut TRAI) the chay sang trai, cong tren | duoi ben trai. Nut 1 2 = cong tren, Nut 3 4 = cong duoi.
 *   Cham vao cong cung chon cong do. Giu Nut 1 = quay lai (tam dung + hoi thoat), giu Nut 2 = bang nhanh (tam dung).
 *
 * Tu vung: kho tu da dong bo tren may (fd_sync_build_spell: theo Bo tu dang chon + uu tien hoc, offline 100%),
 * hoac tu kem trong goi tro choi da cai (fd_game). Khong tai them gi khi choi, khong gui ket qua len may chu (giai doan 1).
 * Nhe: 1 lv_timer 30 ms chi doi vi tri 1 doi tuong (the) -> LVGL chi ve lai 2 vung nho moi khung. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "fd_audio.h"
#include "fd_fonts.h"
#include "fd_game.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "nvs.h"
#include "ui_priv.h"

#define C_OK       0x16A34A
#define C_BAD      0xDC2626
#define C_GATE     0xCBD5E1
#define GM_MAX     200          /* so tu nap cho mot van (xoay vong) */
#define GM_TICK_MS 30           /* ~33 khung/giay */
#define GM_HUD     24
#define GM_LIVES   9
#define FEED_OK_MS  450
#define FEED_BAD_MS 1100
#define OVER_GUARD_MS 900       /* sau khi het van: bo qua nut bam vot trong luc nay */

#define K_SRC  "gm_src"         /* NVS fd: 0 Hung tu dung san, 1 goi da cai */
#define K_DIR  "gm_dir"         /* 0 Viet - Anh (the nghia, cong tu), 1 Anh - Viet, 2 Tron */
#define K_SPD  "gm_spd"         /* 0 Cham, 1 Vua, 2 Nhanh */
#define K_REP  "gm_rep"         /* doc tu tieng Anh khi dung */
#define K_BEST "gm_best"        /* ky luc Hung tu dung san */
#define K_PBST "gm_pbest"       /* ky luc goi da cai */
#define K_PID  "gm_pid"         /* id goi cua gm_pbest */

typedef enum { V_MENU, V_MSG, V_PLAY, V_OVER, V_ONLINE } view_t;
typedef enum { P_RUN, P_FEED } phase_t;

static view_t s_view;
static lv_obj_t *s_body, *s_dlg;
static uint8_t s_src, s_dir, s_spd, s_rep;
static bool s_have_pkg;
static fd_game_cfg_t s_pkg;             /* goi da cai (neu co) */
static fd_game_cfg_t s_cfg;             /* thong so van dang choi */

/* van choi */
static fd_spell_word_t *s_words;        /* PSRAM GM_MAX */
static int s_nw, s_wpos;
static lv_obj_t *s_card, *s_card_lbl, *s_gate[2], *s_gate_lbl[2], *s_score_lbl, *s_level_lbl, *s_life[GM_LIVES];
static lv_timer_t *s_timer;
static phase_t s_phase;
static uint32_t s_t_prev, s_phase_ms, s_over_at;
static float s_prog;
static int32_t s_x0, s_y0, s_x1, s_y1;  /* vi tri dau / cuoi cua the */
static int32_t s_gx, s_gy, s_gw, s_gh;  /* hinh chu nhat cong 0 (cong 1 cung kich thuoc) */
static int s_score, s_lives, s_combo, s_right, s_level, s_fall_ms, s_best, s_ans, s_cur;
static bool s_new_best;

/* tro choi online */
static lv_obj_t *s_on_status, *s_on_list;
static lv_timer_t *s_on_timer;
static fd_game_entry_t *s_entries;      /* PSRAM FD_GAME_LIST_MAX */
static int s_nentries, s_pick;
static fd_game_state_t s_on_seen;

static void show_menu(void);
static void show_play(void);
static void show_over(void);
static void show_online(void);
static void next_card(void);

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

/* ---------- NVS ---------- */
static uint8_t nv_u8(nvs_handle_t h, const char *k, uint8_t def, uint8_t max)
{
    uint8_t v = def;
    nvs_get_u8(h, k, &v);
    return v <= max ? v : def;
}

static void prefs_load(void)
{
    s_src = 0; s_dir = 0; s_spd = 1; s_rep = 0;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        s_src = nv_u8(h, K_SRC, 0, 1);
        s_dir = nv_u8(h, K_DIR, 0, 2);
        s_spd = nv_u8(h, K_SPD, 1, 2);
        s_rep = nv_u8(h, K_REP, 0, 1);
        nvs_close(h);
    }
}

static void pref_save(const char *k, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, k, v);
    nvs_commit(h);
    nvs_close(h);
}

static bool use_pkg(void) { return s_src == 1 && s_have_pkg; }

static int best_load(void)
{
    uint32_t v = 0;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READONLY, &h) != ESP_OK) return 0;
    if (use_pkg()) {
        char id[FD_GAME_ID_LEN] = "";
        size_t n = sizeof(id);
        if (nvs_get_str(h, K_PID, id, &n) == ESP_OK && !strcmp(id, s_pkg.id)) nvs_get_u32(h, K_PBST, &v);
    } else {
        nvs_get_u32(h, K_BEST, &v);
    }
    nvs_close(h);
    return (int)v;
}

static void best_save(int v)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    if (use_pkg()) {
        nvs_set_str(h, K_PID, s_pkg.id);
        nvs_set_u32(h, K_PBST, (uint32_t)v);
    } else {
        nvs_set_u32(h, K_BEST, (uint32_t)v);
    }
    nvs_commit(h);
    nvs_close(h);
}

/* ---------- widget nho ---------- */
static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = ui_label_create(p, t, f, col);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static lv_obj_t *row(lv_obj_t *p)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

static void timers_stop(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_on_timer) { lv_timer_delete(s_on_timer); s_on_timer = NULL; }
}

static void body_reset(void)
{
    timers_stop();
    lv_obj_clean(s_body);
    s_card = s_card_lbl = s_score_lbl = s_level_lbl = s_on_status = s_on_list = NULL;
    memset(s_gate, 0, sizeof(s_gate));
    memset(s_gate_lbl, 0, sizeof(s_gate_lbl));
    memset(s_life, 0, sizeof(s_life));
    lv_obj_set_style_pad_all(s_body, 6, 0);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(s_body, false);
}

static void dlg_close(void)
{
    ui_dialog_close(&s_dlg);
}

/* ---------- MENU ---------- */
static void menu_async(void *a) { (void)a; if (s_body && s_view == V_MENU) show_menu(); }
static void go_menu_async(void *a) { (void)a; if (s_body) show_menu(); }
static void online_async(void *a) { (void)a; if (s_body) show_online(); }
enum { OPT_SRC, OPT_DIR, OPT_SPD, OPT_REP };

static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_SRC) { s_src = v ? 1 : 0; pref_save(K_SRC, s_src); }
    else if (grp == OPT_DIR) { s_dir = (uint8_t)(v <= 2 ? v : 0); pref_save(K_DIR, s_dir); }
    else if (grp == OPT_SPD) { s_spd = (uint8_t)(v <= 2 ? v : 1); pref_save(K_SPD, s_spd); }
    else if (grp == OPT_REP) { s_rep = v ? 1 : 0; pref_save(K_REP, s_rep); }
    lv_async_call(menu_async, NULL);
}

static void start(void);
static void start_async(void *a) { (void)a; if (s_body && (s_view == V_MENU || s_view == V_OVER || s_view == V_MSG)) start(); }
static void begin_cb(lv_event_t *e) { (void)e; lv_async_call(start_async, NULL); }
static void online_cb(lv_event_t *e) { (void)e; lv_async_call(online_async, NULL); }

static void show_menu(void)
{
    s_view = V_MENU;
    dlg_close();
    body_reset();
    s_have_pkg = fd_game_installed(&s_pkg);
    lv_obj_t *sc = ui_setup_begin(s_body, _("GAMES"));
    if (s_have_pkg) {
        const char *src[2] = {"GAME_CATCH", s_pkg.name};
        ui_setup_opt_row(sc, _("GAME_PICK"), src, 2, use_pkg() ? 1 : 0, opt_cb, OPT_SRC, UI_OPT_WRAP);
    } else {
        lv_obj_t *t = label(sc, _("GAME_CATCH"), &fd_font_noto_18, FD_COLOR_TEXT);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_LEFT, 0);
    }
    if (!(use_pkg() && s_pkg.nwords >= 4)) ui_setup_deck(sc, menu_async);
    static const char *const DIR[] = {"SPELL_BUILD_EN", "SPELL_BUILD_VI", "SPELL_DIR_MIX"};
    static const char *const SPD[] = {"GAME_SLOW", "GAME_NORMAL", "GAME_FAST"};
    static const char *const RP[] = {"OFF", "ON"};
    ui_setup_opt_row(sc, _("SEN_DIR"), DIR, 3, s_dir, opt_cb, OPT_DIR, UI_OPT_WRAP);
    ui_setup_opt_row(sc, _("GAME_SPEED"), SPD, 3, s_spd, opt_cb, OPT_SPD, 0);
    ui_setup_opt_row(sc, _("AUTO_PLAY"), RP, 2, s_rep, opt_cb, OPT_REP, UI_OPT_WIDE);
    char tmp[64];
    snprintf(tmp, sizeof(tmp), _("GAME_BEST_N"), best_load());
    lv_obj_t *b = label(sc, tmp, &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *h = label(sc, ui_btn_vertical() ? _("GAME_HOW_V") : _("GAME_HOW_H"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *r = row(sc);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ob = ui_gate_btn(r, _("GAME_ONLINE"), false, online_cb);
    lv_obj_set_flex_grow(ob, 0);
    lv_obj_set_style_pad_hor(ob, 14, 0);
    ui_setup_start(s_body, begin_cb);
}

static void msg_back_cb(lv_event_t *e) { (void)e; lv_async_call(go_menu_async, NULL); }

static void show_msg(const char *msg, const char *hint)
{
    s_view = V_MSG;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(s_body, msg, &fd_font_noto_18, FD_COLOR_TEXT);
    if (hint) label(s_body, hint, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = row(s_body);
    ui_gate_btn(r, _("BACK"), false, msg_back_cb);
    s_view = V_MSG;
}

/* nap tu cho van moi: goi da cai (neu co tu) hoac kho tu dong bo. Chi giu tu co du en + vi. */
static int load_words(void)
{
    if (!s_words) s_words = ps_alloc(GM_MAX * sizeof(fd_spell_word_t));
    if (!s_words) return -1;
    int got = (use_pkg() && s_pkg.nwords >= 4) ? fd_game_pkg_words(s_words, GM_MAX) : fd_sync_build_spell(s_words, GM_MAX);
    int n = 0;
    for (int i = 0; i < got; i++) {
        if (!s_words[i].en[0] || !s_words[i].vi[0]) continue;
        if (n != i) s_words[n] = s_words[i];
        n++;
    }
    return n;
}

static void start(void)
{
    dlg_close();
    s_have_pkg = fd_game_installed(&s_pkg);
    bool pkg_words = use_pkg() && s_pkg.nwords >= 4;
    if (!pkg_words) {
        if (!fd_sync_ready()) {
            bool paired = fd_ota_is_paired();
            show_msg(paired ? _("NO_OFFLINE_DATA_YET") : _("PAIR_TO_STUDY"), paired ? _("SYNC_AUTO_HINT") : _("PAIR_HINT"));
            return;
        }
        if (fd_sync_pick_count(0) <= 0) { fd_ui_toast(_("NO_DECK_WORDS")); show_menu(); return; }
    }
    int n = load_words();
    if (n < 0) { show_msg(_("MEMORY_ERROR"), NULL); return; }
    if (n < 4) { show_msg(_("GAME_FEW_WORDS"), NULL); return; }
    s_nw = n;
    s_wpos = 0;
    if (use_pkg()) s_cfg = s_pkg;
    else fd_game_cfg_default(&s_cfg);
    static const int SPD_PCT[3] = {135, 100, 75};
    s_fall_ms = s_cfg.fall_ms * SPD_PCT[s_spd % 3] / 100;
    if (s_fall_ms < s_cfg.min_ms) s_fall_ms = s_cfg.min_ms;
    s_score = s_combo = s_right = 0;
    s_level = 1;
    s_lives = s_cfg.lives < 1 ? 1 : s_cfg.lives > GM_LIVES ? GM_LIVES : s_cfg.lives;
    s_best = best_load();
    s_new_best = false;
    show_play();
}

/* ---------- CHOI ---------- */
static void hud_update(void)
{
    char t[48];
    if (s_score_lbl) {
        if (s_combo >= 2) snprintf(t, sizeof(t), "  x%d", s_combo > 5 ? 5 : s_combo);
        else t[0] = 0;
        char s[64];
        snprintf(s, sizeof(s), _("GAME_SCORE_N"), s_score);
        strlcat(s, t, sizeof(s));
        lv_label_set_text(s_score_lbl, s);
    }
    if (s_level_lbl) {
        snprintf(t, sizeof(t), _("GAME_LEVEL_N"), s_level);
        lv_label_set_text(s_level_lbl, t);
    }
    for (int i = 0; i < GM_LIVES; i++) {
        if (!s_life[i]) continue;
        lv_obj_set_style_bg_color(s_life[i], lv_color_hex(i < s_lives ? C_BAD : C_GATE), 0);
    }
}

static void gate_style(int g, uint32_t border, uint32_t bg)
{
    if (!s_gate[g]) return;
    lv_obj_set_style_border_color(s_gate[g], lv_color_hex(border), 0);
    lv_obj_set_style_bg_color(s_gate[g], lv_color_hex(bg), 0);
}

static void choose(int g);
static void gate_cb(lv_event_t *e) { choose((int)(intptr_t)lv_event_get_user_data(e)); }

static lv_obj_t *mk_gate(int g, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *f)
{
    lv_obj_t *o = lv_obj_create(s_body);
    lv_obj_remove_style_all(o);
    if (g == 0) { s_gx = x; s_gy = y; s_gw = w; s_gh = h; }
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_border_width(o, 3, 0);
    lv_obj_set_style_pad_all(o, 4, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);
    lv_obj_add_event_cb(o, gate_cb, LV_EVENT_CLICKED, (void *)(intptr_t)g);
    lv_obj_t *l = ui_label_create(o, "", f, FD_COLOR_TEXT);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_set_clickable(l, false);
    s_gate[g] = o;
    s_gate_lbl[g] = l;
    gate_style(g, C_GATE, 0xFFFFFF);
    return o;
}

/* HUD: diem (trai), cap (giua), mang (phai) */
static void mk_hud(int32_t y, int32_t w)
{
    lv_obj_t *hud = lv_obj_create(s_body);
    lv_obj_remove_style_all(hud);
    lv_obj_set_pos(hud, 0, y);
    lv_obj_set_size(hud, w, GM_HUD);
    lv_obj_set_style_pad_hor(hud, 6, 0);
    lv_obj_set_flex_flow(hud, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hud, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(hud, false);
    lv_obj_set_clickable(hud, false);
    s_score_lbl = ui_label_create(hud, "", &fd_font_noto_14, FD_COLOR_TEXT);
    s_level_lbl = ui_label_create(hud, "", &fd_font_noto_14, FD_COLOR_PRIMARY_TEXT);
    lv_obj_t *lv = lv_obj_create(hud);
    lv_obj_remove_style_all(lv);
    lv_obj_set_size(lv, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lv, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(lv, 4, 0);
    lv_obj_set_clickable(lv, false);
    for (int i = 0; i < s_lives && i < GM_LIVES; i++) {
        lv_obj_t *d = lv_obj_create(lv);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_clickable(d, false);
        s_life[i] = d;
    }
}

static void card_place(void)
{
    if (!s_card) return;
    float p = s_prog < 0 ? 0 : s_prog > 1 ? 1 : s_prog;
    lv_obj_set_pos(s_card, s_x0 + (int32_t)((float)(s_x1 - s_x0) * p), s_y0 + (int32_t)((float)(s_y1 - s_y0) * p));
}

/* Dat noi dung the + 2 cong cho cau moi, tinh duong chay theo kich thuoc the. */
static void layout_card(const char *text)
{
    int32_t W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    ui_btn_side_t side = ui_btn_side();
    bool vert = side == UI_BTN_LEFT || side == UI_BTN_RIGHT;
    const lv_font_t *f = &fd_font_noto_18;
    int32_t maxw = vert ? s_gx - 12 : W - 40;
    if (side == UI_BTN_LEFT) maxw = W - (s_gx + s_gw) - 12;
    if (maxw < 60) maxw = 60;
    lv_point_t sz;
    lv_text_get_size(&sz, text, f, 0, 0, maxw - 16, LV_TEXT_FLAG_NONE);
    int32_t lw = sz.x < 20 ? 20 : sz.x + 1;
    lv_obj_set_width(s_card_lbl, lw);
    lv_label_set_text(s_card_lbl, text);
    lv_obj_update_layout(s_card);
    int32_t cw = lv_obj_get_width(s_card), ch = lv_obj_get_height(s_card);
    switch (side) {
    case UI_BTN_BOTTOM: {
        s_x0 = s_x1 = (W - cw) / 2;
        s_y0 = GM_HUD + 2;
        s_y1 = s_gy - ch - 2;
        break;
    }
    case UI_BTN_TOP: {
        s_x0 = s_x1 = (W - cw) / 2;
        s_y0 = H - GM_HUD - 2 - ch;
        s_y1 = s_gy + s_gh + 2;
        break;
    }
    case UI_BTN_RIGHT:
        s_y0 = s_y1 = GM_HUD + (H - GM_HUD - ch) / 2;
        s_x0 = 4;
        s_x1 = s_gx - cw - 4;
        break;
    default: /* UI_BTN_LEFT */
        s_y0 = s_y1 = GM_HUD + (H - GM_HUD - ch) / 2;
        s_x0 = W - cw - 4;
        s_x1 = s_gx + s_gw + 4;
        break;
    }
    if (vert ? (side == UI_BTN_RIGHT ? s_x1 < s_x0 : s_x1 > s_x0) : (side == UI_BTN_BOTTOM ? s_y1 < s_y0 : s_y1 > s_y0)) {
        s_x1 = s_x0;   /* the qua lon: dung yen giua man, van tinh gio */
        s_y1 = s_y0;
    }
}

static void next_card(void)
{
    if (s_wpos >= s_nw) {   /* het vong: xao lai, choi tiep */
        for (int i = s_nw - 1; i > 0; i--) {
            int k = (int)(esp_random() % (uint32_t)(i + 1));
            fd_spell_word_t t = s_words[i]; s_words[i] = s_words[k]; s_words[k] = t;
        }
        s_wpos = 0;
    }
    s_cur = s_wpos++;
    const fd_spell_word_t *w = &s_words[s_cur];
    bool card_vi = s_dir == 0 || (s_dir == 2 && (esp_random() & 1));
    const char *ans = card_vi ? w->en : w->vi;
    const char *alt = NULL;
    for (int t = 0; t < 30 && !alt; t++) {
        const fd_spell_word_t *x = &s_words[esp_random() % (uint32_t)s_nw];
        const char *c = card_vi ? x->en : x->vi;
        if (x != w && c[0] && strcasecmp(c, ans) != 0) alt = c;
    }
    if (!alt) {   /* kho toan tu trung nhau: lay tu ke tiep bat ky */
        const fd_spell_word_t *x = &s_words[(s_cur + 1) % s_nw];
        alt = card_vi ? x->en : x->vi;
    }
    s_ans = (int)(esp_random() & 1);
    lv_label_set_text(s_gate_lbl[s_ans], ans);
    lv_label_set_text(s_gate_lbl[1 - s_ans], alt);
    gate_style(0, C_GATE, 0xFFFFFF);
    gate_style(1, C_GATE, 0xFFFFFF);
    lv_obj_set_hidden(s_card, false);
    layout_card(card_vi ? w->vi : w->en);
    s_prog = 0;
    card_place();
    s_phase = P_RUN;
}

static void feed(bool ok, int chosen)
{
    s_phase = P_FEED;
    s_phase_ms = ok ? FEED_OK_MS : FEED_BAD_MS;
    lv_obj_set_hidden(s_card, true);
    gate_style(s_ans, C_OK, ok ? 0xDCFCE7 : 0xFFFFFF);
    if (chosen >= 0 && chosen != s_ans) gate_style(chosen, C_BAD, 0xFEE2E2);
    const fd_spell_word_t *w = &s_words[s_cur];
    if (ok) {
        s_combo++;
        s_right++;
        s_score += 10 * (s_combo > 5 ? 5 : s_combo);
        if (s_right % s_cfg.every == 0) {
            s_level++;
            s_fall_ms = s_fall_ms * s_cfg.ramp_pct / 100;
            if (s_fall_ms < s_cfg.min_ms) s_fall_ms = s_cfg.min_ms;
        }
        if (s_rep && w->en[0]) {
            const fd_pron_cfg_t pc = {FD_PRON_PICO_ONLY, 1, false};
            ui_pron_say_cfg(w->en, &pc);
        } else {
            fd_audio_feedback(true);
        }
    } else {
        s_combo = 0;
        if (s_lives > 0) s_lives--;
        fd_audio_feedback(false);
    }
    hud_update();
}

static void choose(int g)
{
    if (s_view != V_PLAY || s_phase != P_RUN || s_dlg || ui_quick_is_open() || g < 0 || g > 1) return;
    feed(g == s_ans, g);
}

static bool paused(void) { return s_dlg != NULL || ui_quick_is_open(); }

static void over_async(void *a) { (void)a; if (s_body && s_view == V_PLAY) show_over(); }

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t dt = lv_tick_elaps(s_t_prev);
    s_t_prev = lv_tick_get();
    if (s_view != V_PLAY || paused()) return;
    if (dt > 100) dt = 100;   /* khung bi tre (vd ghi flash): khong nhay coc */
    lv_display_trigger_activity(NULL);   /* dang choi: khong tat man */
    if (s_phase == P_RUN) {
        s_prog += (float)dt / (float)(s_fall_ms > 0 ? s_fall_ms : 1);
        if (s_prog >= 1.0f) {
            s_prog = 1.0f;
            card_place();
            feed(false, -1);   /* cham cong ma chua chon */
        } else {
            card_place();
        }
    } else {
        if (dt >= s_phase_ms) {
            s_phase_ms = 0;
            if (s_lives <= 0) {
                lv_timer_pause(s_timer);
                lv_async_call(over_async, NULL);
            } else {
                next_card();
            }
        } else {
            s_phase_ms -= dt;
        }
    }
}

static void show_play(void)
{
    s_view = V_PLAY;
    body_reset();
    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_set_layout(s_body, LV_LAYOUT_NONE);   /* dat vi tri tuyet doi */
    int32_t W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    ui_btn_side_t side = ui_btn_side();
    switch (side) {
    case UI_BTN_BOTTOM: {
        int32_t gh = 58, gw = W / 2 - 6;
        mk_hud(0, W);
        mk_gate(0, 4, H - gh - 4, gw, gh, &fd_font_noto_18);
        mk_gate(1, W - gw - 4, H - gh - 4, gw, gh, &fd_font_noto_18);
        break;
    }
    case UI_BTN_TOP: {
        int32_t gh = 58, gw = W / 2 - 6;
        mk_hud(H - GM_HUD, W);
        mk_gate(0, 4, 4, gw, gh, &fd_font_noto_18);
        mk_gate(1, W - gw - 4, 4, gw, gh, &fd_font_noto_18);
        break;
    }
    default: {   /* 90 / 270 do: cong xep doc sat cum nut */
        int32_t gw = 104, avail = H - GM_HUD - 8, gh = (avail - 6) / 2;
        int32_t gx = side == UI_BTN_RIGHT ? W - gw - 4 : 4;
        mk_hud(0, W);
        mk_gate(0, gx, GM_HUD + 4, gw, gh, &fd_font_noto_14);
        mk_gate(1, gx, GM_HUD + 4 + gh + 6, gw, gh, &fd_font_noto_14);
        break;
    }
    }
    s_card = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_card);
    lv_obj_set_size(s_card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(FD_COLOR_PRIMARY_TINT), 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_radius(s_card, 10, 0);
    lv_obj_set_style_pad_hor(s_card, 8, 0);
    lv_obj_set_style_pad_ver(s_card, 5, 0);
    lv_obj_set_scrollable(s_card, false);
    lv_obj_set_clickable(s_card, false);
    s_card_lbl = ui_label_create(s_card, "", &fd_font_noto_18, FD_COLOR_TEXT);
    lv_label_set_long_mode(s_card_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_card_lbl, LV_TEXT_ALIGN_CENTER, 0);
    hud_update();
    next_card();
    s_t_prev = lv_tick_get();
    s_timer = lv_timer_create(tick_cb, GM_TICK_MS, NULL);
}

/* ---------- KET THUC ---------- */
static void again_cb(lv_event_t *e) { (void)e; lv_async_call(start_async, NULL); }

static void show_over(void)
{
    if (s_score > s_best) {
        s_new_best = true;
        s_best = s_score;
        best_save(s_score);
    }
    s_view = V_OVER;
    s_over_at = lv_tick_get();
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(s_body, _("GAME_OVER"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    char t[64];
    snprintf(t, sizeof(t), "%d", s_score);
    lv_obj_t *n = lv_label_create(s_body);
    lv_obj_set_style_text_font(n, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(n, lv_color_hex(s_new_best ? C_OK : FD_COLOR_TEXT), 0);
    lv_label_set_text(n, t);
    if (s_new_best && s_score > 0) {
        label(s_body, _("GAME_NEW_BEST"), &fd_font_noto_18, C_OK);
    } else {
        snprintf(t, sizeof(t), _("GAME_BEST_N"), s_best);
        label(s_body, t, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    }
    snprintf(t, sizeof(t), _("GAME_RIGHT_N"), s_right, s_level);
    label(s_body, t, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = row(s_body);
    ui_gate_btn(r, _("BACK"), false, msg_back_cb);
    ui_gate_btn(r, _("SEN_AGAIN"), true, again_cb);
}

/* ---------- TRO CHOI ONLINE ---------- */
static void on_render(void);

static void inst_cb(lv_event_t *e)
{
    (void)e;
    int i = s_pick;
    dlg_close();
    if (i < 0 || i >= s_nentries) return;
    if (!fd_game_install(&s_entries[i])) fd_ui_toast(_("GAME_INSTALL_FAIL"));
    s_on_seen = FD_GAME_IDLE;
    on_render();
}

static void dlg_cancel_cb(lv_event_t *e) { (void)e; dlg_close(); }

static void entry_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_nentries || s_dlg) return;
    const fd_game_entry_t *g = &s_entries[i];
    if (!g->supported) {
        char t[64];
        snprintf(t, sizeof(t), _("GAME_NEED_FW"), g->min_fw[0] ? g->min_fw : "?");
        fd_ui_toast(t);
        return;
    }
    s_pick = i;
    s_dlg = ui_dialog_open();
    lv_obj_t *box = ui_dialog_box(s_dlg);
    char t[160];
    snprintf(t, sizeof(t), _("GAME_INSTALL_Q"), g->name);
    ui_dialog_title(box, t);
    lv_obj_t *r = ui_dialog_btns(box);
    ui_dialog_btn(r, _("CANCEL"), false, dlg_cancel_cb, NULL);
    ui_dialog_btn(r, _("GAME_INSTALL"), true, inst_cb, NULL);
}

static void list_build(void)
{
    if (!s_on_list) return;
    lv_obj_clean(s_on_list);
    if (!s_entries) s_entries = ps_alloc(sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
    s_nentries = s_entries ? fd_game_list(s_entries, FD_GAME_LIST_MAX) : 0;
    for (int i = 0; i < s_nentries; i++) {
        const fd_game_entry_t *g = &s_entries[i];
        lv_obj_t *b = lv_button_create(s_on_list);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_pad_all(b, 8, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(b, entry_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *n = ui_label_create(b, g->name, &fd_font_noto_18, g->supported ? FD_COLOR_TEXT : FD_COLOR_TEXT_MUTED);
        lv_obj_set_width(n, lv_pct(100));
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_clickable(n, false);
        char t[96];
        int kb = (g->size + 1023) / 1024;
        if (g->installed) snprintf(t, sizeof(t), "v%s  %d KB  %s", g->version, kb, _("GAME_INSTALLED_TAG"));
        else if (!g->supported) {
            char nf[48];
            snprintf(nf, sizeof(nf), _("GAME_NEED_FW"), g->min_fw[0] ? g->min_fw : "?");
            snprintf(t, sizeof(t), "v%s  %s", g->version, nf);
        } else snprintf(t, sizeof(t), "v%s  %d KB", g->version, kb);
        lv_obj_t *m = ui_label_create(b, t, &fd_font_noto_14, g->installed ? C_OK : FD_COLOR_TEXT_MUTED);
        lv_obj_set_clickable(m, false);
    }
}

static const char *err_text(const char *err, bool install)
{
    if (!strcmp(err, "no_wifi")) return _("CHECK_WIFI_RETRY");
    if (!strcmp(err, "unpaired")) return _("PAIR_TO_STUDY");
    if (!install && !strcmp(err, "none")) return _("GAME_NONE_ONLINE");
    return install ? _("GAME_INSTALL_FAIL") : _("GAME_LIST_FAIL");
}

static void on_render(void)
{
    if (!s_on_status) return;
    fd_game_status_t st;
    fd_game_get(&st);
    char t[96];
    switch (st.state) {
    case FD_GAME_LISTING:
        lv_label_set_text(s_on_status, _("GAME_LOADING"));
        break;
    case FD_GAME_DOWNLOADING:
        snprintf(t, sizeof(t), _("GAME_INSTALLING"), st.pct);
        lv_label_set_text(s_on_status, t);
        break;
    case FD_GAME_LIST_READY:
        lv_label_set_text(s_on_status, st.n > 0 ? "" : _("GAME_NONE_ONLINE"));
        if (s_on_seen != st.state) list_build();
        break;
    case FD_GAME_DONE:
        if (s_on_seen != st.state) {
            fd_ui_toast(_("GAME_INSTALLED"));
            s_src = 1;
            pref_save(K_SRC, 1);
            s_on_seen = st.state;
            lv_async_call(go_menu_async, NULL);
            return;
        }
        break;
    case FD_GAME_ERROR: {
        const char *m = err_text(st.err, st.install);
        if (!strcmp(st.err, "none") || !strcmp(st.err, "no_wifi") || !strcmp(st.err, "unpaired")) lv_label_set_text(s_on_status, m);
        else { snprintf(t, sizeof(t), "%s (%s)", m, st.err); lv_label_set_text(s_on_status, t); }
        break;
    }
    default:
        break;
    }
    s_on_seen = st.state;
}

static void on_timer_cb(lv_timer_t *t) { (void)t; if (s_view == V_ONLINE) on_render(); }

static void show_online(void)
{
    s_view = V_ONLINE;
    dlg_close();
    body_reset();
    lv_obj_t *t = label(s_body, _("GAME_ONLINE"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_LEFT, 0);
    s_on_status = label(s_body, "", &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    s_on_list = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_on_list);
    lv_obj_set_width(s_on_list, lv_pct(100));
    lv_obj_set_flex_grow(s_on_list, 1);
    lv_obj_set_flex_flow(s_on_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_on_list, 6, 0);
    lv_obj_set_scroll_dir(s_on_list, LV_DIR_VER);
    lv_obj_t *r = row(s_body);
    ui_gate_btn(r, _("BACK"), false, msg_back_cb);
    s_nentries = 0;
    s_on_seen = FD_GAME_IDLE;
    fd_game_status_t st;
    fd_game_get(&st);
    if (st.state != FD_GAME_DOWNLOADING) fd_game_list_fetch();
    on_render();
    s_on_timer = lv_timer_create(on_timer_cb, 300, NULL);
}

/* ---------- thoat giua van ---------- */
static void quit_cb(lv_event_t *e) { (void)e; dlg_close(); lv_async_call(go_menu_async, NULL); }

static void quit_open(void)
{
    s_dlg = ui_dialog_open();
    lv_obj_t *box = ui_dialog_box(s_dlg);
    ui_dialog_title(box, _("GAME_QUIT_Q"));
    lv_obj_t *r = ui_dialog_btns(box);
    ui_dialog_btn(r, _("CANCEL"), false, dlg_cancel_cb, NULL);
    ui_dialog_btn(r, _("QUIT"), true, quit_cb, NULL);
}

/* ---------- screen ---------- */
static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    prefs_load();
    show_menu();
}

static void destroy(void)
{
    timers_stop();
    dlg_close();
    s_body = NULL;
    s_card = s_card_lbl = s_score_lbl = s_level_lbl = s_on_status = s_on_list = NULL;
    memset(s_gate, 0, sizeof(s_gate));
    memset(s_gate_lbl, 0, sizeof(s_gate_lbl));
    memset(s_life, 0, sizeof(s_life));
    free(s_words);
    s_words = NULL;
    free(s_entries);
    s_entries = NULL;
    s_nw = s_nentries = 0;
}

static bool on_button(const fd_evt_button_t *b)
{
    if (b->action == FD_BTN_LONG_PRESS) return false;   /* giu Nut 1 = quay lai, giu Nut 2 = bang nhanh (ui_nav) */
    if (s_dlg) return true;
    int pos = ui_btn_user_pos(b->index);
    if (pos < 0) return true;
    switch (s_view) {
    case V_PLAY: choose(pos < 2 ? 0 : 1); return true;
    case V_MENU: start(); return true;
    case V_OVER: if (lv_tick_elaps(s_over_at) >= OVER_GUARD_MS) start(); return true;
    case V_MSG: show_menu(); return true;
    default: return true;
    }
}

static bool on_back(void)
{
    if (s_dlg) { dlg_close(); return true; }
    if (s_view == V_PLAY) { quit_open(); return true; }
    if (s_view != V_MENU) { show_menu(); return true; }
    return false;
}

const ui_screen_t g_scr_games = {
    .name = "games", .parent = FD_SCR_HOME, .build = build, .destroy = destroy,
    .on_button = on_button, .on_back = on_back,
};
