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
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "fd_audio.h"
#include "fd_berry.h"
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

/* Khoa cu (4.8.0 - 4.9.1), chi doc de chuyen sang luu rieng tung tro choi */
#define K_DIR  "gm_dir"         /* 0 Viet - Anh (the nghia, cong tu), 1 Anh - Viet, 2 Tron */
#define K_SPD  "gm_spd"         /* 0 Cham, 1 Vua, 2 Nhanh */
#define K_REP  "gm_rep"         /* doc tu tieng Anh khi dung */
#define K_BEST "gm_best"        /* ky luc Hung tu dung san */
#define K_PBST "gm_pbest"       /* ky luc goi da cai */
#define K_PID  "gm_pid"         /* id goi cua gm_pbest */
/* 4.9.2 luu rieng tung tro choi (NVS fd): "gs" + fnv1a(id) 8 hex = cai dat, "gr" + 8 hex = bang ky luc top GM_REC_N.
 * id rong = Hung tu dung san. */
#define GM_REC_N 5

typedef struct { uint8_t ver, dir, spd, rep; } gm_set_t;
typedef struct { uint32_t score, when; } gm_rec_t;   /* when = epoch giay, 0 = chua co gio */

/* V_HOME: luoi o tro choi kieu Home. V_MENU: man rieng mot tro choi (Bat dau, Cai dat, Ky luc). */
typedef enum { V_HOME, V_MENU, V_SET, V_REC, V_MSG, V_PLAY, V_OVER, V_BERRY } view_t;
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
static view_t s_left_from;             /* 4.9.2: roi man tu Cai dat (vd mo man Bo tu) -> quay lai Cai dat */
static bool s_gate_pair;                /* 4.9.0: chua dang nhap -> cong Dang nhap chung (nhu cac mo-dun hoc) */

/* 4.9.0 tro choi Berry (goi type berry) */
static bool s_berry_on;
static fd_berry_word_t *s_bwords;       /* tro vao s_words, giu den khi VM dung */
static lv_obj_t *s_bplay, *s_bo[FD_BERRY_OBJ_MAX], *s_bl[FD_BERRY_OBJ_MAX];
static fd_berry_obj_t s_bcur[FD_BERRY_OBJ_MAX];
static int s_blives_max;

/* luoi tro choi (V_HOME) */
static lv_obj_t *s_on_status, *s_grid, *s_dl_lbl;
static lv_timer_t *s_on_timer;
static fd_game_entry_t *s_entries;      /* PSRAM FD_GAME_LIST_MAX, ban sao danh sach may chu */
static int s_nentries, s_dl_idx = -1;
static uint32_t s_list_at;              /* lv_tick lan tai danh sach thanh cong */
static fd_game_state_t s_on_seen;

LV_IMAGE_DECLARE(img_game_basket);
LV_IMAGE_DECLARE(img_game_paw);
LV_IMAGE_DECLARE(img_game_check);
LV_IMAGE_DECLARE(img_game_joystick);

static void show_menu(void);
static void show_home(void);
static void show_play(void);
static void show_over(void);
static void next_card(void);
static void show_berry(void);

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

/* ---------- NVS: cai dat + ky luc rieng tung tro choi ---------- */
static uint8_t nv_u8(nvs_handle_t h, const char *k, uint8_t def, uint8_t max)
{
    uint8_t v = def;
    nvs_get_u8(h, k, &v);
    return v <= max ? v : def;
}

static bool use_pkg(void) { return s_src == 1 && s_have_pkg; }
static const char *cur_id(void) { return use_pkg() ? s_pkg.id : ""; }

static void gm_key(char out[12], char kind, const char *id)
{
    uint32_t h = 2166136261u;
    for (const char *p = id; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    snprintf(out, 12, "g%c%08lx", kind, (unsigned long)h);
}

/* Cai dat tro choi dang chon -> s_dir, s_spd, s_rep. Lan dau: lay khoa cu (gm_dir...) cho moi tro choi. */
static void set_load(void)
{
    s_dir = 0; s_spd = 1; s_rep = 0;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READONLY, &h) != ESP_OK) return;
    char k[12];
    gm_key(k, 's', cur_id());
    gm_set_t g;
    size_t n = sizeof(g);
    if (nvs_get_blob(h, k, &g, &n) == ESP_OK && n == sizeof(g)) {
        s_dir = g.dir <= 2 ? g.dir : 0;
        s_spd = g.spd <= 2 ? g.spd : 1;
        s_rep = g.rep ? 1 : 0;
    } else {
        s_dir = nv_u8(h, K_DIR, 0, 2);
        s_spd = nv_u8(h, K_SPD, 1, 2);
        s_rep = nv_u8(h, K_REP, 0, 1);
    }
    nvs_close(h);
}

static void set_save(void)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    char k[12];
    gm_key(k, 's', cur_id());
    gm_set_t g = {1, s_dir, s_spd, s_rep};
    nvs_set_blob(h, k, &g, sizeof(g));
    nvs_commit(h);
    nvs_close(h);
}

/* Bang ky luc (giam dan). Tra so muc. Lan dau: lay ky luc cu (gm_best / gm_pbest) lam muc dau (khong co ngay). */
static int rec_load(gm_rec_t out[GM_REC_N])
{
    memset(out, 0, sizeof(gm_rec_t) * GM_REC_N);
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READONLY, &h) != ESP_OK) return 0;
    char k[12];
    gm_key(k, 'r', cur_id());
    size_t n = sizeof(gm_rec_t) * GM_REC_N;
    if (nvs_get_blob(h, k, out, &n) != ESP_OK || n != sizeof(gm_rec_t) * GM_REC_N) {
        memset(out, 0, sizeof(gm_rec_t) * GM_REC_N);
        uint32_t v = 0;
        if (use_pkg()) {
            char id[FD_GAME_ID_LEN] = "";
            size_t m = sizeof(id);
            if (nvs_get_str(h, K_PID, id, &m) == ESP_OK && !strcmp(id, s_pkg.id)) nvs_get_u32(h, K_PBST, &v);
        } else {
            nvs_get_u32(h, K_BEST, &v);
        }
        out[0].score = v;
    }
    nvs_close(h);
    int c = 0;
    while (c < GM_REC_N && out[c].score > 0) c++;
    return c;
}

static int best_load(void)
{
    gm_rec_t r[GM_REC_N];
    return rec_load(r) > 0 ? (int)r[0].score : 0;
}

/* Them diem vao bang (neu lot top). */
static void rec_add(int score)
{
    if (score <= 0) return;
    gm_rec_t r[GM_REC_N];
    rec_load(r);
    int pos = GM_REC_N;
    for (int i = 0; i < GM_REC_N; i++) if ((uint32_t)score > r[i].score) { pos = i; break; }
    if (pos >= GM_REC_N) return;
    memmove(&r[pos + 1], &r[pos], sizeof(gm_rec_t) * (GM_REC_N - 1 - pos));
    time_t now = time(NULL);
    r[pos].score = (uint32_t)score;
    r[pos].when = now > 1700000000 ? (uint32_t)now : 0;   /* chua co gio SNTP -> khong ghi ngay */
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    char k[12];
    gm_key(k, 'r', cur_id());
    nvs_set_blob(h, k, r, sizeof(r));
    nvs_commit(h);
    nvs_close(h);
}

/* Icon o: truong "icon" cua goi, khong co thi theo type (catch = gio, berry = can dieu khien). */
static const lv_image_dsc_t *game_icon(const char *icon, bool berry)
{
    if (icon && !strcmp(icon, "paw")) return &img_game_paw;
    if (icon && !strcmp(icon, "check")) return &img_game_check;
    if (icon && !strcmp(icon, "basket")) return &img_game_basket;
    if (icon && !strcmp(icon, "joystick")) return &img_game_joystick;
    return berry ? &img_game_joystick : &img_game_basket;
}

/* 4.9.3 so sanh phien ban goi: tung phan cach dau cham, so truoc (1.10 > 1.9), phan chu con lai so theo strcmp. */
static int ver_cmp(const char *a, const char *b)
{
    while (*a || *b) {
        unsigned long na = 0, nb = 0;
        bool da = false, db = false;
        while (*a >= '0' && *a <= '9') { na = na * 10 + (unsigned long)(*a++ - '0'); da = true; }
        while (*b >= '0' && *b <= '9') { nb = nb * 10 + (unsigned long)(*b++ - '0'); db = true; }
        if (da != db) return da ? 1 : -1;   /* co so > khong co (1.0 > 1.) */
        if (na != nb) return na > nb ? 1 : -1;
        const char *ea = a, *eb = b;
        while (*ea && *ea != '.') ea++;
        while (*eb && *eb != '.') eb++;
        size_t la = (size_t)(ea - a), lb = (size_t)(eb - b);
        int c = strncmp(a, b, la < lb ? la : lb);
        if (c) return c > 0 ? 1 : -1;
        if (la != lb) return la < lb ? 1 : -1;   /* 1.0.0 > 1.0.0-beta */
        a = ea; b = eb;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

/* Trang thai goi so voi danh sach may chu: G_NONE chua cai, G_OK da cai ban moi nhat (hoac moi hon), G_OLD da cai ban cu.
 * Chua co danh sach (khong mang) thi khong biet ban moi -> goi dang cai van choi duoc. */
typedef enum { G_NONE, G_OK, G_OLD } gstate_t;
static gstate_t entry_state(const fd_game_entry_t *g)
{
    if (!s_have_pkg || strcmp(s_pkg.id, g->id) != 0) return G_NONE;
    return ver_cmp(s_pkg.version, g->version) >= 0 ? G_OK : G_OLD;
}

static const fd_game_entry_t *listed_pkg(void)
{
    if (!s_have_pkg || !s_entries) return NULL;
    for (int i = 0; i < s_nentries; i++) if (!strcmp(s_entries[i].id, s_pkg.id)) return &s_entries[i];
    return NULL;
}

static bool pkg_outdated(void)
{
    const fd_game_entry_t *g = listed_pkg();
    return g && entry_state(g) == G_OLD;
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

static void berry_stop(void)
{
    if (s_berry_on) {
        fd_berry_stop();
        s_berry_on = false;
    }
    free(s_bwords);
    s_bwords = NULL;
    s_bplay = NULL;
    memset(s_bo, 0, sizeof(s_bo));
    memset(s_bl, 0, sizeof(s_bl));
}

static void body_reset(void)
{
    timers_stop();
    berry_stop();
    lv_obj_clean(s_body);
    s_card = s_card_lbl = s_score_lbl = s_level_lbl = s_on_status = s_grid = s_dl_lbl = NULL;
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

/* ---------- MAN RIENG MOT TRO CHOI: Bat dau, Cai dat, Ky luc ---------- */
static void show_settings(void);
static void show_records(void);
static void menu_async(void *a) { (void)a; if (s_body) show_menu(); }
static void go_menu_async(void *a) { (void)a; if (s_body) show_menu(); }
static void set_async(void *a) { (void)a; if (s_body && s_view == V_SET) show_settings(); }
static void set_open_async(void *a) { (void)a; if (s_body) show_settings(); }
static void settings_cb(lv_event_t *e) { (void)e; lv_async_call(set_open_async, NULL); }
enum { OPT_DIR, OPT_SPD, OPT_REP };

static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_DIR) s_dir = (uint8_t)(v <= 2 ? v : 0);
    else if (grp == OPT_SPD) s_spd = (uint8_t)(v <= 2 ? v : 1);
    else if (grp == OPT_REP) s_rep = v ? 1 : 0;
    set_save();
    lv_async_call(set_async, NULL);
}

static void start(void);
static void start_async(void *a) { (void)a; if (s_body && (s_view == V_MENU || s_view == V_OVER || s_view == V_MSG || s_view == V_HOME)) start(); }
static void begin_cb(lv_event_t *e) { (void)e; lv_async_call(start_async, NULL); }
static void rec_async(void *a) { (void)a; if (s_body) show_records(); }
static void records_cb(lv_event_t *e) { (void)e; lv_async_call(rec_async, NULL); }
static void back_menu_cb(lv_event_t *e) { (void)e; lv_async_call(menu_async, NULL); }

static const char *cur_name(void) { return use_pkg() ? s_pkg.name : _("GAME_CATCH"); }
static bool cur_berry(void) { return use_pkg() && s_pkg.berry; }
static bool cur_deck_words(void) { return !(use_pkg() && s_pkg.nwords >= 4); }   /* dung kho tu cua may */

static void show_menu(void)
{
    s_view = V_MENU;
    dlg_close();
    body_reset();
    s_have_pkg = fd_game_installed(&s_pkg);
    if (s_src == 1 && !s_have_pkg) s_src = 0;
    set_load();
    lv_obj_t *sc = ui_setup_begin(s_body, cur_name());
    lv_obj_t *top = row(sc);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ic = lv_image_create(top);
    lv_image_set_src(ic, use_pkg() ? game_icon(s_pkg.icon, s_pkg.berry) : &img_game_basket);
    char tmp[64];
    int best = best_load();
    if (best > 0) snprintf(tmp, sizeof(tmp), _("GAME_BEST_N"), best);
    else strlcpy(tmp, _("GAME_NO_RECORDS"), sizeof(tmp));
    lv_obj_t *b = ui_label_create(top, tmp, &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_flex_grow(b, 1);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    if (use_pkg()) {
        const fd_game_entry_t *lg = listed_pkg();
        bool old = lg && entry_state(lg) == G_OLD;
        char vt[80];
        if (old) snprintf(vt, sizeof(vt), _("GAME_VERSION_OLD"), s_pkg.version, lg->version);
        else snprintf(vt, sizeof(vt), _("GAME_VERSION_N"), s_pkg.version);
        lv_obj_t *vl = label(sc, vt, &fd_font_noto_14, old ? C_BAD : FD_COLOR_TEXT_MUTED);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_LEFT, 0);
    }
    lv_obj_t *h = label(sc, cur_berry() ? _("GAME_BERRY_HOW") : ui_btn_vertical() ? _("GAME_HOW_V") : _("GAME_HOW_H"),
                        &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *r = row(sc);
    ui_gate_btn(r, _("SETTINGS"), false, settings_cb);
    ui_gate_btn(r, _("GAME_RECORDS"), false, records_cb);
    ui_setup_start(s_body, begin_cb);
}

/* Cai dat rieng tro choi: Hung tu (dung san hoac goi catch) = bo tu (neu dung kho tu may), chieu, toc do, doc tu;
 * goi Berry = doc tu (fd.say) - chieu va toc do do script tu quyet. */
static void show_settings(void)
{
    s_view = V_SET;
    dlg_close();
    body_reset();
    s_have_pkg = fd_game_installed(&s_pkg);
    if (s_src == 1 && !s_have_pkg) s_src = 0;
    set_load();
    char t[80];
    snprintf(t, sizeof(t), "%s - %s", _("SETTINGS"), cur_name());
    lv_obj_t *sc = ui_setup_begin(s_body, t);
    static const char *const DIR[] = {"SPELL_BUILD_EN", "SPELL_BUILD_VI", "SPELL_DIR_MIX"};
    static const char *const SPD[] = {"GAME_SLOW", "GAME_NORMAL", "GAME_FAST"};
    static const char *const RP[] = {"OFF", "ON"};
    if (cur_deck_words()) ui_setup_deck(sc, set_async);
    if (!cur_berry()) {
        ui_setup_opt_row(sc, _("SEN_DIR"), DIR, 3, s_dir, opt_cb, OPT_DIR, UI_OPT_WRAP);
        ui_setup_opt_row(sc, _("GAME_SPEED"), SPD, 3, s_spd, opt_cb, OPT_SPD, 0);
    }
    ui_setup_opt_row(sc, _("AUTO_PLAY"), RP, 2, s_rep, opt_cb, OPT_REP, UI_OPT_WIDE);
    lv_obj_t *r = row(s_body);
    ui_gate_btn(r, _("BACK"), false, back_menu_cb);
}

static void show_records(void)
{
    s_view = V_REC;
    dlg_close();
    body_reset();
    char t[96];
    snprintf(t, sizeof(t), "%s - %s", _("GAME_RECORDS"), cur_name());
    lv_obj_t *sc = ui_setup_begin(s_body, t);
    gm_rec_t r[GM_REC_N];
    int n = rec_load(r);
    if (n == 0) label(sc, _("GAME_NO_RECORDS"), &fd_font_noto_18, FD_COLOR_TEXT_MUTED);
    for (int i = 0; i < n; i++) {
        lv_obj_t *ln = row(sc);
        lv_obj_set_flex_align(ln, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        snprintf(t, sizeof(t), "%d.  %lu", i + 1, (unsigned long)r[i].score);
        ui_label_create(ln, t, &fd_font_noto_18, i == 0 ? C_OK : FD_COLOR_TEXT);
        t[0] = 0;
        if (r[i].when) {
            time_t w = (time_t)r[i].when;
            struct tm lt;
            localtime_r(&w, &lt);
            strftime(t, sizeof(t), "%d/%m/%Y", &lt);
        }
        ui_label_create(ln, t, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    }
    lv_obj_t *rr = row(s_body);
    ui_gate_btn(rr, _("BACK"), false, back_menu_cb);
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
    if (s_src == 1 && !s_have_pkg) s_src = 0;
    if (use_pkg() && pkg_outdated()) {   /* 4.9.3: biet co ban moi thi bat buoc cap nhat moi choi */
        fd_ui_toast(_("GAME_NEED_UPDATE"));
        show_home();
        return;
    }
    set_load();
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
    if (use_pkg() && s_pkg.berry) show_berry();
    else show_play();
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

/* ---------- TRO CHOI BERRY (4.9.0) ---------- */
static void btap_cb(lv_event_t *e)
{
    if (s_view != V_BERRY || paused()) return;
    fd_berry_event(FD_BERRY_EV_TAP, (int)(intptr_t)lv_event_get_user_data(e));
}

static void berry_hud(const fd_berry_misc_t *m)
{
    char t[48];
    if (s_score_lbl) {
        snprintf(t, sizeof(t), _("GAME_SCORE_N"), m->score);
        lv_label_set_text(s_score_lbl, t);
    }
    if (s_level_lbl) {
        if (m->level > 0) snprintf(t, sizeof(t), _("GAME_LEVEL_N"), m->level);
        else t[0] = 0;
        lv_label_set_text(s_level_lbl, t);
    }
    for (int i = 0; i < GM_LIVES; i++) {
        if (!s_life[i]) continue;
        lv_obj_set_hidden(s_life[i], m->lives < 0 || i >= s_blives_max);
        lv_obj_set_style_bg_color(s_life[i], lv_color_hex(i < m->lives ? C_BAD : C_GATE), 0);
    }
}

static const lv_font_t *bfont(uint8_t f)
{
    return f >= 32 ? &fd_font_mont_32 : f >= 18 ? &fd_font_noto_18 : &fd_font_noto_14;
}

static void berry_apply(int i, const fd_berry_obj_t *o)
{
    if (!s_bplay) return;
    if (!s_bo[i]) {
        lv_obj_t *b = lv_obj_create(s_bplay);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_pad_all(b, 4, 0);
        lv_obj_set_scrollable(b, false);
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, btap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_clickable(l, false);
        s_bo[i] = b;
        s_bl[i] = l;
        memset(&s_bcur[i], 0, sizeof(s_bcur[i]));
        s_bcur[i].font = 0xFF;   /* buoc ap dung lan dau */
    }
    lv_obj_t *b = s_bo[i], *l = s_bl[i];
    fd_berry_obj_t *c = &s_bcur[i];
    lv_obj_set_hidden(b, !o->used || !o->vis);
    if (c->x != o->x || c->y != o->y) lv_obj_set_pos(b, o->x, o->y);
    if (c->font != o->font) {
        lv_obj_set_style_text_font(l, bfont(o->font), 0);
    }
    if (c->w != o->w || c->font == 0xFF) {
        if (o->w > 0) {
            lv_obj_set_width(b, o->w);
            lv_obj_set_width(l, lv_pct(100));
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        } else {
            lv_obj_set_width(b, LV_SIZE_CONTENT);
            lv_obj_set_width(l, LV_SIZE_CONTENT);
        }
    }
    if (c->h != o->h || c->font == 0xFF) {
        lv_obj_set_height(b, o->h > 0 ? o->h : LV_SIZE_CONTENT);
        lv_obj_center(l);
    }
    if (c->bg != o->bg || c->font == 0xFF) {
        lv_obj_set_style_bg_opa(b, o->bg < 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        if (o->bg >= 0) lv_obj_set_style_bg_color(b, lv_color_hex((uint32_t)o->bg), 0);
    }
    if (c->fg != o->fg || c->font == 0xFF) lv_obj_set_style_text_color(l, lv_color_hex((uint32_t)o->fg), 0);
    if (strcmp(c->txt, o->txt) != 0 || c->font == 0xFF) lv_label_set_text(l, o->txt);
    *c = *o;
}

static void berry_err_async(void *a);
static void berry_over_async(void *a);
static char s_berr[96];

static void berry_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_view != V_BERRY || !s_berry_on) return;
    bool p = paused();
    fd_berry_pause(p);
    if (!p) lv_display_trigger_activity(NULL);
    static fd_berry_obj_t objs[FD_BERRY_OBJ_MAX];
    fd_berry_misc_t m;
    uint32_t d = fd_berry_sync(objs, &m);
    for (int i = 0; i < FD_BERRY_OBJ_MAX; i++)
        if (d & (1u << i)) berry_apply(i, &objs[i]);
    if (m.hud_dirty) {
        if (m.lives > s_blives_max) s_blives_max = m.lives > GM_LIVES ? GM_LIVES : m.lives;
        s_score = m.score;
        s_level = m.level;
        berry_hud(&m);
    }
    if (m.say[0]) {
        if (s_rep) {
            const fd_pron_cfg_t pc = {FD_PRON_PICO_ONLY, 1, false};
            ui_pron_say_cfg(m.say, &pc);
        } else {
            fd_audio_feedback(true);
        }
    } else if (m.sound >= 0) {
        fd_audio_feedback(m.sound == 1);
    }
    if (m.state == FD_BERRY_OVER) {
        s_score = m.over_score;
        lv_timer_pause(s_timer);
        lv_async_call(berry_over_async, NULL);
    } else if (m.state == FD_BERRY_ERROR) {
        strlcpy(s_berr, m.err, sizeof(s_berr));
        lv_timer_pause(s_timer);
        lv_async_call(berry_err_async, NULL);
    }
}

static void berry_over_async(void *a) { (void)a; if (s_body && s_view == V_BERRY) show_over(); }
static void berry_err_async(void *a)
{
    (void)a;
    if (!s_body || s_view != V_BERRY) return;
    char hint[96];
    strlcpy(hint, s_berr, sizeof(hint));
    show_msg(_("GAME_SCRIPT_ERR"), hint);
}

static void show_berry(void)
{
    size_t len = 0;
    char *script = fd_game_pkg_script(&len);
    if (!script) { show_msg(_("GAME_INSTALL_FAIL"), NULL); return; }
    s_view = V_BERRY;
    body_reset();
    s_bwords = malloc(sizeof(fd_berry_word_t) * (size_t)s_nw);
    if (!s_bwords) { free(script); show_msg(_("MEMORY_ERROR"), NULL); return; }
    for (int i = 0; i < s_nw; i++) { s_bwords[i].en = s_words[i].en; s_bwords[i].vi = s_words[i].vi; }
    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_set_layout(s_body, LV_LAYOUT_NONE);
    int32_t W = lv_display_get_horizontal_resolution(NULL), H = lv_display_get_vertical_resolution(NULL);
    ui_btn_side_t side = ui_btn_side();
    bool top = side == UI_BTN_TOP;
    s_lives = GM_LIVES;   /* mk_hud tao du cham, berry_hud an bot */
    s_blives_max = 0;
    mk_hud(top ? H - GM_HUD : 0, W);
    for (int i = 0; i < GM_LIVES; i++) if (s_life[i]) lv_obj_set_hidden(s_life[i], true);
    s_bplay = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_bplay);
    lv_obj_set_pos(s_bplay, 0, top ? 0 : GM_HUD);
    lv_obj_set_size(s_bplay, W, H - GM_HUD);
    lv_obj_set_scrollable(s_bplay, false);
    lv_obj_set_clickable(s_bplay, false);
    static const char *const SIDE[] = {"top", "right", "bottom", "left"};
    int si = side == UI_BTN_TOP ? 0 : side == UI_BTN_RIGHT ? 1 : side == UI_BTN_BOTTOM ? 2 : 3;
    fd_berry_cfg_t cfg = {
        .script = script, .len = len, .words = s_bwords, .nwords = s_nw, .w = W, .h = H - GM_HUD, .side = SIDE[si],
    };
    s_score = 0;
    s_level = 0;
    s_right = -1;
    if (!fd_berry_start(&cfg)) {
        free(script);
        show_msg(_("MEMORY_ERROR"), NULL);
        return;
    }
    s_berry_on = true;
    fd_berry_misc_t m = {.score = 0, .lives = -1, .level = 0};
    berry_hud(&m);
    s_timer = lv_timer_create(berry_tick_cb, GM_TICK_MS, NULL);
}

/* ---------- KET THUC ---------- */
static void again_cb(lv_event_t *e) { (void)e; lv_async_call(start_async, NULL); }

static void show_over(void)
{
    if (s_score > s_best && s_score > 0) {
        s_new_best = true;
        s_best = s_score;
    }
    rec_add(s_score);   /* 4.9.2: bang ky luc rieng tung tro choi */
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
    if (s_right >= 0) {
        snprintf(t, sizeof(t), _("GAME_RIGHT_N"), s_right, s_level);
        label(s_body, t, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    }
    lv_obj_t *r = row(s_body);
    ui_gate_btn(r, _("BACK"), false, msg_back_cb);
    ui_gate_btn(r, _("SEN_AGAIN"), true, again_cb);
}

/* ---------- LUOI TRO CHOI (kieu Home) ----------
 * O dau: Hung tu dung san (tu vung tren may). Sau do moi tro choi trong danh sach may chu (icon theo goi).
 * Chua tai (hoac khac ban dang cai) = o mo; cham = tai ve (tien do hien tren o) roi vao choi ngay.
 * Da tai = cham mo man rieng cua tro choi (Bat dau, Cai dat, Ky luc). Chi 1 goi cai: cai game khac thi game cu mo lai.
 * Khong mang: van hien o Hung tu + goi dang cai. */
#define GT_H_PORT 84
#define GT_GAP_COL 8
#define GT_GAP_ROW 12
#define GT_NATIVE  (-1)
#define GT_PKG     (-2)   /* goi dang cai nhung khong co trong danh sach (khong mang) */
#define LIST_TTL_MS 60000

static void on_render(void);
static void open_menu_async(void *a) { (void)a; if (s_body && s_view == V_HOME) show_menu(); }
static void play_new_async(void *a)
{
    (void)a;
    if (!s_body || s_view != V_HOME) return;
    s_src = 1;
    start();   /* 4.9.1: tai xong choi ngay */
}


static void tile_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_dlg || s_view != V_HOME) return;
    fd_game_status_t st;
    fd_game_get(&st);
    if (st.state == FD_GAME_DOWNLOADING) return;   /* dang tai mot tro choi */
    if (i == GT_NATIVE) { s_src = 0; lv_async_call(open_menu_async, NULL); return; }
    if (i == GT_PKG) { s_src = 1; lv_async_call(open_menu_async, NULL); return; }
    if (i < 0 || i >= s_nentries) return;
    const fd_game_entry_t *g = &s_entries[i];
    if (!g->supported) {
        char t[64];
        snprintf(t, sizeof(t), _("GAME_NEED_FW"), g->min_fw[0] ? g->min_fw : "?");
        fd_ui_toast(t);
        return;
    }
    if (entry_state(g) == G_OK) { s_src = 1; lv_async_call(open_menu_async, NULL); return; }
    /* chua cai hoac ban cu (G_OLD): tai ban moi, cai de len slot; cai dat + ky luc giu theo id */
    if (!fd_game_install(g)) { fd_ui_toast(_("GAME_INSTALL_FAIL")); return; }
    s_dl_idx = i;
    /* tien do hien ngay tren nhan o vua cham (khong dung lai luoi trong callback cua chinh o do) */
    lv_obj_t *tb = lv_event_get_current_target(e);
    s_dl_lbl = lv_obj_get_child(tb, 1);
    if (s_dl_lbl) {
        char t[48];
        snprintf(t, sizeof(t), _("GAME_DL_PCT"), 0);
        lv_label_set_text(s_dl_lbl, t);
    }
    s_on_seen = FD_GAME_IDLE;
    on_render();
}

static lv_obj_t *tile(lv_obj_t *grid, int32_t w, const lv_image_dsc_t *icon, const char *name, bool dim, int code)
{
    /* giong o Home (scr_home.c tile): nen trong, icon 3D 48 px + ten Noto 14 toi da 2 dong */
    lv_obj_t *b = lv_button_create(grid);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, GT_H_PORT);
    lv_obj_set_style_radius(b, 12, LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 0, 0);
    if (dim) lv_obj_set_style_opa(b, LV_OPA_40, 0);   /* chua tai = mo */
    lv_obj_t *ic = lv_image_create(b);
    lv_image_set_src(ic, icon);
    lv_obj_set_clickable(ic, false);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x111827), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(l, -4, 0);
    lv_obj_set_style_max_height(l, 40, 0);
    lv_label_set_text(l, name);
    lv_obj_set_clickable(l, false);
    lv_obj_add_event_cb(b, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    return l;
}

static void grid_build(void)
{
    if (!s_grid) return;
    lv_obj_clean(s_grid);
    s_dl_lbl = NULL;
    s_have_pkg = fd_game_installed(&s_pkg);
    int32_t gw = lv_display_get_horizontal_resolution(NULL) - 12;
    int32_t tw = (gw - 2 * GT_GAP_COL) / 3;
    tile(s_grid, tw, &img_game_basket, _("GAME_CATCH"), false, GT_NATIVE);
    bool pkg_listed = false, any_dim = false;
    for (int i = 0; i < s_nentries; i++) {
        const fd_game_entry_t *g = &s_entries[i];
        gstate_t gs = entry_state(g);
        bool inst = gs == G_OK;
        if (s_have_pkg && !strcmp(s_pkg.id, g->id)) pkg_listed = true;   /* khac ban = o mo, cham de cap nhat */
        char nm[96];
        if (i == s_dl_idx) {
            fd_game_status_t st;
            fd_game_get(&st);
            snprintf(nm, sizeof(nm), _("GAME_DL_PCT"), st.pct);
        } else if (!g->supported) {
            snprintf(nm, sizeof(nm), _("GAME_NEED_FW"), g->min_fw[0] ? g->min_fw : "?");
        } else if (gs == G_OLD) {
            strlcpy(nm, _("GAME_NEED_UPDATE"), sizeof(nm));   /* 4.9.3: ban cu = mo + Can cap nhat, cham de tai */
        } else {
            strlcpy(nm, g->name, sizeof(nm));
        }
        any_dim = any_dim || !inst;
        lv_obj_t *l = tile(s_grid, tw, game_icon(g->icon, g->berry), nm, !inst, i);
        if (i == s_dl_idx) s_dl_lbl = l;
    }
    if (s_have_pkg && !pkg_listed) tile(s_grid, tw, game_icon(s_pkg.icon, s_pkg.berry), s_pkg.name, false, GT_PKG);
    if (s_on_status && any_dim && s_dl_idx < 0) lv_label_set_text(s_on_status, _("GAME_TAP_HINT"));
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
    if (!s_on_status || s_view != V_HOME) return;
    fd_game_status_t st;
    fd_game_get(&st);
    char t[96];
    bool first = s_on_seen != st.state;
    s_on_seen = st.state;
    switch (st.state) {
    case FD_GAME_LISTING:
        lv_label_set_text(s_on_status, _("GAME_LOADING"));
        break;
    case FD_GAME_DOWNLOADING:
        if (s_dl_lbl) {
            snprintf(t, sizeof(t), _("GAME_DL_PCT"), st.pct);
            lv_label_set_text(s_dl_lbl, t);
        }
        lv_label_set_text(s_on_status, "");
        break;
    case FD_GAME_LIST_READY:
        if (first) {
            if (!s_entries) s_entries = ps_alloc(sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
            s_nentries = s_entries ? fd_game_list(s_entries, FD_GAME_LIST_MAX) : 0;
            s_list_at = lv_tick_get();
            lv_label_set_text(s_on_status, s_nentries > 0 ? "" : _("GAME_NONE_ONLINE"));
            grid_build();
        }
        break;
    case FD_GAME_DONE:
        if (first) {
            s_dl_idx = -1;
            lv_async_call(play_new_async, NULL);
        }
        break;
    case FD_GAME_ERROR:
        if (first) {
            const char *m = err_text(st.err, st.install);
            if (st.install) {
                fd_ui_toast(m);
                s_dl_idx = -1;
                grid_build();
            }
            if (!strcmp(st.err, "none") || !strcmp(st.err, "no_wifi") || !strcmp(st.err, "unpaired")) lv_label_set_text(s_on_status, m);
            else { snprintf(t, sizeof(t), "%s (%s)", m, st.err); lv_label_set_text(s_on_status, t); }
        }
        break;
    default:
        break;
    }
}

static void on_timer_cb(lv_timer_t *t) { (void)t; if (s_view == V_HOME) on_render(); }

static void show_home(void)
{
    s_view = V_HOME;
    dlg_close();
    body_reset();
    lv_obj_set_scrollable(s_body, true);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_t *t = label(s_body, _("GAMES"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_LEFT, 0);
    s_grid = lv_obj_create(s_body);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_size(s_grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_scrollable(s_grid, false);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_grid, GT_GAP_ROW, 0);
    lv_obj_set_style_pad_column(s_grid, GT_GAP_COL, 0);
    s_on_status = label(s_body, "", &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    s_on_seen = FD_GAME_IDLE;
    fd_game_status_t st;
    fd_game_get(&st);
    if (st.state != FD_GAME_DOWNLOADING) s_dl_idx = -1;
    grid_build();
    /* tai lai danh sach khi chua co hoac da cu (> 60 s); tranh nhay luoi moi lan quay ve */
    bool stale = s_nentries == 0 || lv_tick_elaps(s_list_at) > LIST_TTL_MS;
    if (st.state != FD_GAME_DOWNLOADING && stale) fd_game_list_fetch();
    else s_on_seen = st.state;   /* khong dung lai luoi cho trang thai cu */
    on_render();
    s_on_timer = lv_timer_create(on_timer_cb, 300, NULL);
}

/* ---------- thoat giua van ---------- */
static void dlg_cancel_cb(lv_event_t *e) { (void)e; dlg_close(); }
static void quit_cb(lv_event_t *e) { (void)e; dlg_close(); lv_async_call(go_menu_async, NULL); }   /* ve man rieng tro choi */

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
    s_gate_pair = !fd_ota_is_paired();   /* 4.9.0: nhu Luyen tap va cac mo-dun hoc: chua dang nhap thi khong choi */
    if (s_gate_pair) { s_view = V_HOME; s_left_from = V_HOME; ui_pair_gate(s_body); return; }
    if (s_left_from == V_SET) show_settings();   /* s_src giu nguyen (bien tinh) */
    else { s_src = 0; show_home(); }
    s_left_from = V_HOME;
}

static void destroy(void)
{
    s_left_from = s_body ? s_view : V_HOME;
    timers_stop();
    berry_stop();
    dlg_close();
    s_body = NULL;
    s_card = s_card_lbl = s_score_lbl = s_level_lbl = s_on_status = s_grid = s_dl_lbl = NULL;
    memset(s_gate, 0, sizeof(s_gate));
    memset(s_gate_lbl, 0, sizeof(s_gate_lbl));
    memset(s_life, 0, sizeof(s_life));
    s_dl_idx = -1;
    free(s_words);
    s_words = NULL;
    free(s_entries);
    s_entries = NULL;
    s_nw = s_nentries = 0;
}

static bool on_button(const fd_evt_button_t *b)
{
    if (s_gate_pair) return false;   /* cong Dang nhap: hanh vi chung nhu cac mo-dun hoc */
    if (b->action == FD_BTN_LONG_PRESS) return false;   /* giu Nut 1 = quay lai, giu Nut 2 = bang nhanh (ui_nav) */
    if (s_dlg) return true;
    int pos = ui_btn_user_pos(b->index);
    if (pos < 0) return true;
    switch (s_view) {
    case V_PLAY: choose(pos < 2 ? 0 : 1); return true;
    case V_BERRY: if (!paused()) fd_berry_event(FD_BERRY_EV_BUTTON, pos + 1); return true;
    case V_MENU: start(); return true;
    case V_OVER: if (lv_tick_elaps(s_over_at) >= OVER_GUARD_MS) start(); return true;
    case V_MSG: show_menu(); return true;
    default: return true;   /* luoi, Cai dat, Ky luc: chi cham (giong Home) */
    }
}

static bool on_back(void)
{
    if (s_gate_pair) return false;
    if (s_dlg) { dlg_close(); return true; }
    if (s_view == V_PLAY || s_view == V_BERRY) { quit_open(); return true; }
    if (s_view == V_MENU) { show_home(); return true; }
    if (s_view != V_HOME) { show_menu(); return true; }
    return false;
}

const ui_screen_t g_scr_games = {
    .name = "games", .parent = FD_SCR_HOME, .build = build, .destroy = destroy,
    .on_button = on_button, .on_back = on_back,
};
