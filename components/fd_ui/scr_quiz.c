/* Hoc tap = Trac nghiem tu API may chu (docs/study/CONTRACT-firmware.md). KHONG con dung bo tu tren may.
 *   SETUP  : chieu, so cau (10/20/Vo tan), nguon (Bo the / Offline; 23/10 bo Mockup), nut [Bat dau] ghim day man (cai dat cuon phia tren; 23/10 bo nut tai bo tu offline). Nho trong NVS fd/quiz_cfg.
 *   LOAD   : POST /api/study/start trong task fd_study (khong chan LVGL); man nay doc trang thai bang lv_timer.
 *   MSG    : chua ghep (-> Ghép thiết bị) / chua Wi-Fi (-> Mở Wi-Fi) / loi mang (Thử lại).
 *   QUIZ   : "Cau i/n" (Vo tan: "Cau i") + diem (khong co nut bo qua: moi cau deu phai tra loi), de bai (+ IPA), 4 dap an luoi 2x2 TO MAU PHIM (A,B,C,D = TL,TR,BL,BR).
 *            Dung/sai theo `answer`, tu sang cau sau sau 900 ms.
 *   KET QUA: cham tai may; moi phien server (session_id rieng) lam xong -> /data/outbox (fd_deck gui /api/study/result khi online, 409 = da nhan).
 *   RESULT : dung/so cau da lam + % (cham tai may), "Học tiếp" / "Về Home". Vo tan: thoat giua bai -> RESULT.
 * Quay lai: QUIZ -> hoi "Thoat bai lam?" -> SETUP; RESULT/MSG/LOAD -> SETUP; SETUP -> Home. */
#include <stdio.h>
#include <string.h>
#include "fd_audio.h"
#include "fd_deck.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_study.h"
#include "esp_log.h"
#include "nvs.h"
#include "ui_priv.h"

#define C_OK  0x16A34A
#define C_BAD 0xDC2626

typedef enum { V_SETUP, V_LOAD, V_MSG, V_QUIZ, V_RESULT } view_t;
static view_t s_view;
static lv_obj_t *s_body, *s_dlg;
static lv_obj_t *s_ans[4];
static lv_obj_t *s_lbl[4];   /* chu dap an trong o (o la hang flex: [cham mau][chu]) */
static lv_obj_t *s_ipa;      /* IPA (Viet-Anh: hien sau khi tra loi) */
static lv_timer_t *s_next_t, *s_poll_t;
 

static uint8_t s_src = 1, s_dir, s_ncfg = 0;   /* s_ncfg: 0/1/2 = 10/20/Vo tan (23/10; NVS ver QCFG_VER); s_src: 1 deck, 2 offline (0 mockup: bo khoi UI 23/10); s_dir: 0 en-vi, 1 vi-en */
static const int NOPT[3] = {10, 20, 0};   /* 0 = Vo tan */
#define QCFG_VER 1u   /* quiz_cfg bit 24..31: cu (5/10/20) -> 5,10 = 10; 20 = 20 */
static uint8_t s_reps = 0;       /* tu dong phat: 0 Tat, 1/2 lan; NVS fd/pron_cfg bit 8..15, bit 24..31 = PRON_VER */
#define PRON_VER 2u               /* 23/10: cfg cu (khong co ver) -> Tat 1 lan, lua chon sau do giu nguyen */
static uint32_t s_pron_keep;     /* cac bit cu cua pron_cfg (nguon/tai nen, bo khong dung) giu nguyen khi ghi */
static int s_pos, s_score, s_done;   /* s_pos: chi so cau toan bai (noi nhieu phien); s_done: so cau da tra loi */
static bool s_wait;                  /* V_LOAD giua bai: cho nap phien tiep */
static bool s_answered;

static void show_setup(void);
static void show_quiz(void);
static void next_cb(lv_timer_t *t);
static void answer(int i);
static void ans_cb(lv_event_t *e);
static void speak_cb(lv_event_t *e);

/* ===== BAN CHINH THUC (17:15): KHONG con chu A/B/C/D tren man hoc tap - nhan biet dap an CHI bang MAU phim. =====
 * Mau phim (chi so k): 0 = HONG PASTEL KEY_PINK 0xF9A8D4 (09:30; 09:21 la 0xEC4899; truoc do TRANG), 1 = CAM 0xF97316, 2 = XANH LA 0x16A34A, 3 = DO 0xDC2626.
 * NOI DUNG dap an CO DINH theo o (lua chon 1..4 = TL, TR, BL, BR o moi huong); chi MAU o di chuyen theo xoay.
 * Bam phim mau X = chon o dang to mau X. Moi lan bam anh xa DUNG MOT LAN: nut -> mau (KEY_GPIO_COLOUR) -> o (COLOUR_CELL).
 * Ve man dung nghich dao cua CUNG bang COLOUR_CELL (colour_at_cell) -> khong co anh xa kep. */
enum { KC_PINK = 0, KC_ORANGE = 1, KC_GREEN = 2, KC_RED = 3 };

/* ===== KEY_GPIO_COLOUR: nut B1..B4 (board_pins.h, chi so b->index) -> MAU phim VAT LY cua nut do. =====
 * DA XAC NHAN TREN PHAN CUNG: 180 do (20/10 09:42) GPIO3 hong TL, GPIO21 cam TR, GPIO2 do BL, GPIO14 xanh la BR;
 *   0 do (20/10 09:49) GPIO14 xanh TL, GPIO2 do TR, GPIO21 cam BL, GPIO3 hong BR.
 * (Lich su: 09:35 doi hong/xanh, 09:36 doi cam/do, 09:39 doi cam/xanh.) Xem .cursor/docs/key-mapping.md. */
static const uint8_t KEY_GPIO_COLOUR[4] = {
    KC_ORANGE,   /* B1 = GPIO21 = CAM     (thu phan cung 20/10 09:39) */
    KC_RED,      /* B2 = GPIO2  = DO      (thu phan cung 20/10 09:36) */
    KC_PINK,     /* B3 = GPIO3  = HONG    (thu phan cung 20/10 09:35) */
    KC_GREEN,    /* B4 = GPIO14 = XANH LA (thu phan cung 20/10 09:39) */
};

/* COLOUR_CELL[xoay][mau] = o tren man: 0 = tren-trai TL, 1 = tren-phai TR, 2 = duoi-trai BL, 3 = duoi-phai BR.
 * Phim co dinh tren than may, noi dung man xoay. Suy ra tu bang anh xa cam ung board_orient.h / BOARD_ORIENTS
 * (toa do tho FT6336U tx 0..239, ty 0..319; LCD swap/mirror dong bo voi touch):
 *   idx0   0 do : x = ty,        y = 239 - tx
 *   idx1  90 do : x = tx,        y = ty
 *   idx2 180 do : x = 319 - ty,  y = tx        <- HUONG MAC DINH; nguoi dung xac nhan cam ngang: phim HONG (cu: trang) o tren-trai
 *   idx3 270 do : x = 239 - tx,  y = 319 - ty
 * Moc 180 do (DA XAC NHAN 20/10 09:42): hong TL, cam TR, DO BL, XANH BR  =>  vi tri tho: hong (tx nho, ty lon),
 *   cam (tx nho, ty nho), do (tx lon, ty lon), xanh (tx lon, ty nho). Thay vao cong thuc tung huong:
 *   0 do  : hong (x=ty lon, y=239-tx lon) BR, cam BL, do (x lon, y nho) TR, xanh (x nho, y nho) TL  (= 180 do xoay nua vong; nguoi dung xoay may theo man -> cum phim sang ben TRAI)
 *   90 do : hong (x=tx nho, y=ty lon) BL, cam TL, do (x lon, y lon) BR, xanh (x lon, y nho) TR
 *   270 do: hong (x=239-tx lon, y=319-ty nho) TR, cam BR, do (x nho, y nho) TL, xanh (x nho, y lon) BL
 * Cum phim theo goc nhin nguoi dung (nguoi dung xoay CA may cung man): 180 do cum o BEN PHAI man (gan mep ty = 0 cua
 *   tam tho), 0 do o BEN TRAI (ca hai DA XAC NHAN). Trong cum: cam/xanh sat mep ty = 0, hong/do lui vao (ty lon hon);
 *   hong/cam o tx nho, do/xanh o tx lon.
 *   90 do (x = tx, y = ty): mep ty = 0 la MEP TREN -> cum phim o PHIA TREN man. hong (x nho, y lon hon) BL, cam (x nho,
 *     y nho) TL, do (x lon, y lon hon) BR, xanh (x lon, y nho) TR.
 *   270 do (x = 239 - tx, y = 319 - ty): mep ty = 0 la MEP DUOI -> cum phim o PHIA DUOI man. hong (x lon, y nho hon) TR,
 *     cam (x lon, y lon) BR, do (x nho, y nho hon) TL, xanh (x nho, y lon) BL.
 * (Truoc 09:33 do/xanh bi dao o ca 4 hang. 180/0 DA XAC NHAN; 90/270 suy tu hai huong da xac nhan, chua thu tren may.) */
static const uint8_t COLOUR_CELL[4][4] = {
    /* hong cam xanh do */
    {3, 2, 0, 1},   /* idx0   0 do : hong BR, cam BL, xanh TL, do TR (DA XAC NHAN 09:49; cum phim ben trai) */
    {2, 0, 1, 3},   /* idx1  90 do : hong BL, cam TL, xanh TR, do BR (cum phim phia tren) */
    {0, 1, 3, 2},   /* idx2 180 do (mac dinh): hong TL, cam TR, xanh BR, do BL (DA XAC NHAN 09:42; cum phim ben phai) */
    {1, 3, 2, 0},   /* idx3 270 do : hong TR, cam BR, xanh BL, do TL (cum phim phia duoi) */
};

/* O tren man (0..3) -> mau (0..3) theo huong xoay hien tai (nghich dao COLOUR_CELL) */
static int colour_at_cell(int cell)
{
    int rot = fd_display_get_rotation() & 3;
    for (int k = 0; k < 4; k++) {
        if (COLOUR_CELL[rot][k] == cell) return k;
    }
    return cell;
}

int ui_key_colour(int idx) { return (idx >= 0 && idx < 4) ? KEY_GPIO_COLOUR[idx] : -1; }

int ui_key_cell(int idx)
{
    int k = ui_key_colour(idx);
    return k < 0 ? -1 : COLOUR_CELL[fd_display_get_rotation() & 3][k];
}

const char *ui_key_colour_name(int k)
{
    static const char *const N[4] = {"PINK", "ORANGE", "GREEN", "RED"};
    return (k >= 0 && k < 4) ? N[k] : "?";
}

/* Mau phim (chi so k) -> mau VIEN o + CHAM tron trong o (ui_answer_cell, ui_widgets.c). Nen o trung tinh, chu toi.
 * Hong: MOT hang so KEY_PINK (hong pastel pha trang, 20/10 09:30). */
#define KEY_PINK 0xF9A8D4
static const uint32_t KEY_COLOUR[4] = {KEY_PINK, 0xF97316, 0x16A34A, 0xDC2626};
static const uint32_t KEY_GLYPH[4] = {0x111827, 0x111827, 0x111827, 0xFFFFFF};   /* OK/X trong cham: toi tren hong/cam/xanh, trang tren do */

/* Bang mau/xoay dung chung */
int ui_cell_colour(int cell) { return colour_at_cell(cell); }
uint32_t ui_key_rgb(int k) { return (k >= 0 && k < 4) ? KEY_COLOUR[k] : 0x9CA3AF; }
uint32_t ui_key_glyph(int k) { return (k >= 0 && k < 4) ? KEY_GLYPH[k] : 0x111827; }

/* ---------- NVS ---------- */
static void cfg_save(void);
static void cfg_load(void)
{
    nvs_handle_t h;
    uint32_t v;
    bool migrate = false;
    if (nvs_open("fd", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u32(h, "quiz_cfg", &v) == ESP_OK) {
        s_src = ((v & 0xFF) == 1 || (v & 0xFF) == 2) ? (uint8_t)(v & 0xFF) : 1;   /* NVS cu = Mockup (0)/sai -> 1 Bo the (online) */
        uint32_t n = (v >> 8) & 0xFF;
        if ((v >> 24) == QCFG_VER) s_ncfg = n < 3 ? (uint8_t)n : 0;
        else { s_ncfg = n == 2 ? 1 : 0; migrate = true; }   /* cu: 0/1/2 = 5/10/20 -> 10/10/20 */
        s_dir = ((v >> 16) & 0xFF) ? 1 : 0;
    }
    if (nvs_get_u32(h, "pron_cfg", &v) == ESP_OK) {
        uint32_t r = (v >> 8) & 0xFF;
        s_reps = (v >> 24) == PRON_VER ? (r == 1 || r == 2 ? (uint8_t)r : 0) : 0;
        s_pron_keep = v & 0x00FF00FFu;
        if ((v >> 24) != PRON_VER) migrate = true;
    }
    nvs_close(h);
    if (migrate) { cfg_save(); ESP_LOGI("scr_quiz", "pron_cfg: chuyen cau hinh cu -> Tu dong phat = Tat"); }
}

static void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "quiz_cfg", (uint32_t)s_src | ((uint32_t)s_ncfg << 8) | ((uint32_t)s_dir << 16) | (QCFG_VER << 24));
    nvs_set_u32(h, "pron_cfg", s_pron_keep | ((uint32_t)s_reps << 8) | (PRON_VER << 24));
    nvs_commit(h);
    nvs_close(h);
}

static void stop_timer(void)
{
    if (s_next_t) {
        lv_timer_delete(s_next_t);
        s_next_t = NULL;
    }
}

static void stop_poll(void)
{
    if (s_poll_t) {
        lv_timer_delete(s_poll_t);
        s_poll_t = NULL;
    }
}

static void body_reset(void)
{
    stop_timer();
    stop_poll();
    lv_obj_clean(s_body);
    s_ipa = NULL;
    for (int i = 0; i < 4; i++) s_ans[i] = s_lbl[i] = NULL;
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, t);
    return l;
}

static lv_obj_t *center_label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = label(p, t, f, color);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t *hrow(lv_obj_t *p, int32_t h)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    return r;
}

static lv_obj_t *btn(lv_obj_t *p, const char *t, bool primary, lv_event_cb_t cb, intptr_t ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_height(b, 36);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
    lv_obj_set_style_border_width(b, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_hor(b, 6, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)ud);
    return b;
}

/* hang 2 nut lon (44 px) chia deu */
static void btn_pair_row(const char *a, lv_event_cb_t ca, const char *b, lv_event_cb_t cb)
{
    lv_obj_t *r = hrow(s_body, 44);
    lv_obj_set_style_pad_column(r, 8, 0);
    if (a) lv_obj_set_flex_grow(btn(r, a, false, ca, 0), 1);
    if (b) lv_obj_set_flex_grow(btn(r, b, true, cb, 0), 1);
    for (uint32_t i = 0; i < lv_obj_get_child_count(r); i++) lv_obj_set_height(lv_obj_get_child(r, (int32_t)i), 44);
}

static const char *dir_str(void) { return s_dir ? "vi-en" : "en-vi"; }

/* ---------- SETUP ---------- */
enum { OPT_SRC, OPT_N, OPT_DIR, OPT_REP };

static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_SRC) s_src = (uint8_t)v + 1;   /* nut 0/1 = deck/offline */
    else if (grp == OPT_N) s_ncfg = (uint8_t)v;
    else if (grp == OPT_DIR) s_dir = (uint8_t)v;
    else if (grp == OPT_REP) { s_reps = (uint8_t)v; cfg_save(); }   /* nut Tat/1 lan/2 lan -> 0/1/2 */
    show_setup();
}

static void opt_row(lv_obj_t *p, const char *name, int grp, const char *const *opts, int n, int sel)
{
    /* 4 hang lua chon: ngang (240 cao) 32 px, doc 38 px */
    /* 23/10: hang cao theo noi dung (truoc: 32 px ngang < nut 36 px -> nut bi cat vi hang clip con) */
    int32_t bh = lv_display_get_horizontal_resolution(NULL) > lv_display_get_vertical_resolution(NULL) ? 34 : 36;
    lv_obj_t *r = hrow(p, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(r, false);
    lv_obj_t *l = label(r, name, NULL, FD_COLOR_TEXT_MUTED);
    bool por = lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL);
    if (grp == OPT_REP) {   /* "Tu dong phat" dai: rong hon + xuong 2 dong khi doc */
        lv_obj_set_width(l, por ? 56 : 92);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(l, -3, 0);
    } else lv_obj_set_width(l, por ? 48 : 62);   /* doc: nhuong cho 3 nut lua chon */
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = btn(r, opts[i], i == sel, opt_cb, (grp << 4) | i);
        lv_obj_set_height(b, bh);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 2, 0);   /* doc: "Offline"/"Bo the" vua nut */
        lv_obj_t *bl = lv_obj_get_child(b, 0);
        lv_obj_set_width(bl, lv_pct(100)); lv_label_set_long_mode(bl, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void poll_cb(lv_timer_t *t);

static void show_load(const char *txt, view_t v)
{
    s_view = v;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sp = lv_spinner_create(s_body);
    lv_obj_set_size(sp, 44, 44);
    lv_obj_set_style_arc_width(sp, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, lv_color_hex(FD_COLOR_PRIMARY), LV_PART_INDICATOR);
    center_label(s_body, txt, &fd_font_noto_18, FD_COLOR_TEXT);
    s_poll_t = lv_timer_create(poll_cb, 200, NULL);
}

#define OFFLINE_MSG "Kh\xC3\xB4ng c\xC3\xB3 Wifi, h\xE1\xBB\x8D" "c offline"   /* 23/10: toast khi hoc offline VA thuc su khong co WiFi */
static const char *const SRC_API[3] = {"mockup", "deck", "offline"};

static void start_cb(lv_event_t *e)
{
    (void)e;
    cfg_save();
    /* 23/10: bo hang Nguon. Co WiFi + da ghep -> bo the online; khong mang -> bo tu offline da luu (neu co). */
    /* 23/10: cache-first - con phien chua dung trong cache cho chieu nay -> hien ngay, fd_deck tai bu o nen. */
    /* 23/10: bai = chuoi phien (fd_study): cache chua dung -> study/start -> cache bat ky. Khong WiFi: chi cache. */
    bool nowifi = !fd_net_is_connected();
    if (!fd_study_start(dir_str(), NOPT[s_ncfg], SRC_API[nowifi ? 2 : 1])) return;
    if (nowifi && fd_deck_ready()) fd_ui_toast(OFFLINE_MSG);
    s_wait = false;
    show_load("Đang tải câu hỏi…", V_LOAD);
}

/* ---------- Phat am Hoc tap: CHI Pico (khong file bo tu / cache, khong tai nen); lap x1/x2 chinh tren man cau hoi ---------- */
/* manual: bam loa (Tat -> 1 lan); tu dong: Tat -> khong phat */
static void quiz_say(const char *t, bool manual)
{
    if (!manual && s_reps == 0) return;
    const fd_pron_cfg_t c = {FD_PRON_PICO_ONLY, s_reps ? s_reps : 1, false};
    ui_pron_say_cfg(t, &c);
}

static void show_setup(void)
{
    s_view = V_SETUP;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 4, 0);
    /* 23/10: cai dat trong vung cuon (flex_grow) phia tren; nut Bat dau ghim day man, khong de len hang cuoi */
    lv_obj_t *sc = lv_obj_create(s_body);
    lv_obj_remove_style_all(sc);
    lv_obj_set_width(sc, lv_pct(100));
    lv_obj_set_flex_grow(sc, 1);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sc, 6, 0);
    lv_obj_set_style_pad_bottom(sc, 4, 0);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *tr = hrow(sc, 26);
    lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(tr, "Học tập", &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);

    static const char *const DIR[] = {"Anh-Việt", "Việt-Anh"};
    static const char *const NS[] = {"10", "20", "V\xC3\xB4 t\xE1\xBA\xADn"};
    opt_row(sc, "Chiều", OPT_DIR, DIR, 2, s_dir);
    static const char *const RP[] = {"T\xE1\xBA\xAFt", "1 l\xE1\xBA\xA7n", "2 l\xE1\xBA\xA7n"};
    opt_row(sc, "Số câu", OPT_N, NS, 3, s_ncfg);
    opt_row(sc, "T\xE1\xBB\xB1 \xC4\x91\xE1\xBB\x99ng ph\xC3\xA1t", OPT_REP, RP, 3, s_reps);
    lv_obj_t *b = btn(s_body, "B\xE1\xBA\xAFt \xC4\x91\xE1\xBA\xA7u", true, start_cb, 0);
    lv_obj_set_size(b, lv_pct(100), 46);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 6, 0);
    lv_obj_set_style_shadow_offset_y(b, 2, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *bl = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_font(bl, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(bl, 1, 0);
}

/* ---------- MSG (chua ghep / chua Wi-Fi / loi) ---------- */
static void home_cb(lv_event_t *e) { (void)e; ui_nav_home(); }
static void back_setup_cb(lv_event_t *e) { (void)e; show_setup(); }
static void open_pair_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_PAIR); }
static void open_wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }
static void retry_start_cb(lv_event_t *e) { start_cb(e); }

static void show_msg(const fd_study_status_t *st)
{
    s_view = V_MSG;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    const char *t, *sub = "", *bt = "Thử lại";
    lv_event_cb_t cb = retry_start_cb;
    char tmp[64];
    switch (st->err) {
    case FD_STUDY_ERR_UNPAIRED:
        t = "Cần ghép thiết bị để học";
        sub = "Ghép máy với tài khoản trên điện thoại.";
        bt = "Ghép thiết bị";
        cb = open_pair_cb;
        break;
    case FD_STUDY_ERR_NO_WIFI:
        t = "Chưa kết nối Wi-Fi";
        sub = "Học tập cần Wi-Fi để tải câu hỏi.";
        bt = "Mở Wi-Fi";
        cb = open_wifi_cb;
        break;
    case FD_STUDY_ERR_NET:
        t = "Không kết nối được máy chủ";
        sub = "Kiểm tra Wi-Fi rồi thử lại.";
        break;
    case FD_STUDY_ERR_NO_DECK:
        t = "Ch\xC6\xB0" "a c\xC3\xB3 d\xE1\xBB\xAF li\xE1\xBB\x87u offline";
        sub = "K\xE1\xBA\xBFt n\xE1\xBB\x91i Wi-Fi \xC4\x91\xE1\xBB\x83 t\xE1\xBB\xB1 t\xE1\xBA\xA3i c\xC3\xA2u h\xE1\xBB\x8Fi.";
        bt = "OK";
        cb = back_setup_cb;
        break;
    case FD_STUDY_ERR_FORMAT:
        t = "Dữ liệu câu hỏi không hợp lệ";
        break;
    default:
        snprintf(tmp, sizeof(tmp), "Máy chủ báo lỗi (%d)", st->http);
        t = tmp;
        break;
    }
    center_label(s_body, t, &fd_font_noto_18, st->err == FD_STUDY_ERR_UNPAIRED ? FD_COLOR_TEXT : C_BAD);
    if (sub[0]) center_label(s_body, sub, NULL, FD_COLOR_TEXT_MUTED);
    btn_pair_row("Trở lại", back_setup_cb, bt, cb);
}

/* ---------- RESULT ---------- */
static void again_cb(lv_event_t *e) { start_cb(e); }   /* Hoc tiep: bo moi, cung cau hinh */

static void show_result(void)
{
    s_view = V_RESULT;
    body_reset();
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    int c = s_score, n = s_done;   /* cham tai may; ket qua tung phien da vao outbox */
    int sc = n ? c * 100 / n : 0;
    center_label(s_body, "Kết quả", &fd_font_noto_18, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = lv_obj_create(s_body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_t *a = label(r, "", &fd_font_mont_32, FD_COLOR_PRIMARY_TEXT);
    lv_label_set_text_fmt(a, "%d/%d", c, n);
    lv_obj_t *p = label(r, "", &fd_font_noto_18, sc >= 50 ? C_OK : C_BAD);
    lv_label_set_text_fmt(p, "đúng · %d%%", sc);
    btn_pair_row("Về Home", home_cb, "Học tiếp", again_cb);
}

/* Ket thuc bai -> tong ket. complete: phien lam do dang cung gui; !complete (thoat Vo tan): giu tien do .part */
static void end_quiz(bool complete)
{
    stop_timer();
    fd_study_finish(complete);
    show_result();
}

/* doc trang thai fd_study (task rieng) trong luong LVGL */
static void poll_cb(lv_timer_t *t)
{
    (void)t;
    fd_study_status_t st;
    fd_study_get(&st);
    if (s_view != V_LOAD) return;
    if (s_wait) {   /* giua bai: cho phien tiep */
        if (fd_study_q(s_pos)) { s_wait = false; show_quiz(); }
        else if (fd_study_exhausted()) { s_wait = false; end_quiz(true); }
        return;
    }
    if (st.state == FD_STUDY_READY) {
        s_pos = s_score = s_done = 0;
        show_quiz();
    } else if (st.state == FD_STUDY_ERROR) show_msg(&st);
}

/* ---------- QUIZ ---------- */

static void next_cb(lv_timer_t *t)
{
    (void)t;
    s_next_t = NULL;   /* repeat 1: LVGL tu xoa */
    int len = fd_study_len();
    if (++s_pos >= len && len) end_quiz(true);
    else if (fd_study_q(s_pos)) show_quiz();
    else if (fd_study_exhausted()) end_quiz(true);   /* Vo tan: het nguon */
    else { show_load("Đang tải câu hỏi…", V_LOAD); s_wait = true; }
}

static void answer(int i)
{
    if (s_answered || s_dlg || s_view != V_QUIZ) return;
    const fd_study_q_t *q = fd_study_q(s_pos);
    if (!q) return;
    s_answered = true;
    int c = q->answer;
    s_done++;
    if (i == c) s_score++;
    /* Phan hoi (ui_answer_cell_mark): OK trong cham mau cua o dung; chon sai -> X trong cham o da chon + mo + gach chu. */
    for (int k = 0; k < 4; k++) {
        if (!s_ans[k]) continue;
        uint32_t gl = KEY_GLYPH[colour_at_cell(k)];
        ui_answer_cell_mark(s_ans[k], s_lbl[k], k == c ? UI_ANS_CORRECT : (k == i ? UI_ANS_WRONG : UI_ANS_OTHER), gl);
    }
    if (s_ipa) lv_obj_set_hidden(s_ipa, false);   /* Viet-Anh: hien IPA tu dung sau khi tra loi */
    fd_audio_feedback(i == c);   /* am dung/sai truoc, phat am tu (neu co) xep sau trong hang doi */
    if (s_dir == 1 && c >= 0 && c < 4) quiz_say(q->choice[c], false);   /* Viet-Anh: doc tu dung SAU khi tra loi (khong lo dap an); khong doi thoi gian 0.9/1.5 s */
    fd_study_answer(s_pos, i);   /* tien do .part / phien xong -> outbox (sau khi doc q: slot co the duoc giai phong) */
    s_next_t = lv_timer_create(next_cb, i == c ? 900 : 1500, NULL);
    lv_timer_set_repeat_count(s_next_t, 1);
}

static void ans_cb(lv_event_t *e) { answer((int)(intptr_t)lv_event_get_user_data(e)); }

/* Anh-Viet: nut loa canh tu tieng Anh (de bai) -> doc bat ky luc nao (duong chung ui_pron_say). */
static void speak_cb(lv_event_t *e)
{
    (void)e;
    const fd_study_q_t *q = fd_study_q(s_pos);
    if (s_dir == 0 && q) quiz_say(q->prompt, true);
}

static void show_quiz(void)
{
    const fd_study_q_t *q = fd_study_q(s_pos);
    if (!q) { show_setup(); return; }
    s_view = V_QUIZ;
    body_reset();
    s_answered = false;
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_body, 4, 0);

    lv_obj_t *top = hrow(s_body, 24);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *pg = label(top, "", NULL, FD_COLOR_TEXT_MUTED);
    int len = fd_study_len();
    if (len) lv_label_set_text_fmt(pg, "Câu %d/%d", s_pos + 1, len);
    else lv_label_set_text_fmt(pg, "Câu %d", s_pos + 1);   /* Vo tan */
    lv_obj_t *sc = label(top, "", NULL, FD_COLOR_PRIMARY_TEXT);
    lv_label_set_text_fmt(sc, "Điểm %d", s_score);

    /* de bai */
    lv_obj_t *pr = lv_obj_create(s_body);
    lv_obj_remove_style_all(pr);
    lv_obj_set_width(pr, lv_pct(100));
    lv_obj_set_flex_grow(pr, 1);
    lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (s_dir == 0) {   /* Anh-Viet: tu tieng Anh + loa + IPA */
        lv_obj_t *r = lv_obj_create(pr);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 8, 0);
        /* tu ngan: Montserrat 32 (chi ASCII); dai/co dau: Noto 18 (co Latin mo rong + IPA) */
        bool big = strlen(q->prompt) <= 12;
        for (const unsigned char *p = (const unsigned char *)q->prompt; big && *p; p++) if (*p >= 0x80) big = false;
        lv_obj_t *w = label(r, q->prompt, big ? &fd_font_mont_32 : &fd_font_noto_18, FD_COLOR_TEXT);
        if (!big) { lv_obj_set_style_max_width(w, LV_MIN(220, lv_display_get_horizontal_resolution(NULL) - 64), 0);   /* doc: chu + loa 40 + khe 8 vua 228 px */ lv_label_set_long_mode(w, LV_LABEL_LONG_WRAP); }
        lv_obj_t *spk = lv_button_create(r);
        lv_obj_set_size(spk, 40, 40);
        lv_obj_set_style_radius(spk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(spk, 0, 0);
        lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY), 0);
        lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY_DARK), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(spk, 0, 0);
        lv_obj_add_event_cb(spk, speak_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *si = label(spk, LV_SYMBOL_VOLUME_MAX, &fd_font_mont_24, FD_COLOR_ON_PRIMARY);
        lv_obj_center(si);
        if (q->ipa[0]) label(pr, q->ipa, NULL, FD_COLOR_PRIMARY_TEXT);   /* Noto 14: co IPA Extensions */
    } else {            /* Viet-Anh: nghia tieng Viet; IPA tu dung hien sau khi tra loi */
        lv_obj_t *m = center_label(pr, q->prompt, &fd_font_noto_18, FD_COLOR_TEXT);
        (void)m;
        if (q->ipa[0]) {
            s_ipa = label(pr, q->ipa, NULL, FD_COLOR_PRIMARY_TEXT);
            lv_obj_set_hidden(s_ipa, true);
        }
    }

    /* 4 dap an 2x2: lua chon A,B,C,D = o TL,TR,BL,BR; mau o theo phim vat ly */
    lv_obj_t *g = lv_obj_create(s_body);
    lv_obj_remove_style_all(g);
    /* 23/10: luoi 2x2 A B / C D ca doc lan ngang; doc: o 64 px (chu toi da 3 dong), 2 x 64 + 4 = 132 px. */
    bool por = (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL));
    lv_obj_set_size(g, lv_pct(100), por ? 2 * 64 + 4 : 2 * 48 + 6);
    if ((fd_display_get_rotation() & 3) == 1) lv_obj_move_to_index(g, 1);   /* 90 do: cum phim phia tren */
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(g, por ? 4 : 6, 0);
    lv_obj_set_style_pad_column(g, 6, 0);
    /* LVGL flex: o co flex_grow duoc tinh 0 px khi xet xuong dong (ROW_WRAP) -> ca 4 o nam 1 hang. Dung rong co dinh = nua vung chua. */
    lv_obj_update_layout(s_body);
    int32_t cw = (lv_obj_get_content_width(g) - 6) / 2;
    if (cw < 40) cw = 40;
    for (int cell = 0; cell < 4; cell++) {
        int k = colour_at_cell(cell);
        lv_obj_t *l;
        lv_obj_t *b = ui_answer_cell(g, KEY_COLOUR[k], q->choice[cell], &l);   /* 23/10: bo tien to A./B./C./D. - chi chu dap an */
        lv_obj_set_size(b, cw, por ? 64 : 48);   /* 2 o/hang: rong = (rong luoi - khe 6) / 2 */
        if (por && l) lv_obj_set_height(l, 60);   /* doc: toi da 3 dong Noto 14, qua thi "..." */
        if (l) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        s_ans[cell] = b;
        s_lbl[cell] = l;
        lv_obj_add_event_cb(b, ans_cb, LV_EVENT_CLICKED, (void *)(intptr_t)cell);
    }
}

/* ---------- hop thoai "Thoat bai lam?" ---------- */
static void dlg_close(void)
{
    if (s_dlg) {
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
}

/* Thoat giua bai: Vo tan -> tong ket (phien xong da vao outbox, phien do dang giu .part); 10/20 -> ve cai dat (giu .part) */
static void quit_quiz(void)
{
    if (!fd_study_len() && s_done > 0) { end_quiz(false); return; }
    fd_study_finish(false);
    show_setup();
}

static void dlg_cb(lv_event_t *e)
{
    bool ok = (bool)(intptr_t)lv_event_get_user_data(e);
    dlg_close();
    if (ok) quit_quiz();
    else if (s_view == V_QUIZ && s_answered && !s_next_t) {
        s_next_t = lv_timer_create(next_cb, 900, NULL);
        lv_timer_set_repeat_count(s_next_t, 1);
    }
}

static void dlg_btn(lv_obj_t *row, const char *t, bool primary)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
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
    lv_obj_set_style_text_font(box, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    label(box, "Thoát bài làm?", &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_t *row = hrow(box, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    dlg_btn(row, "Hủy", false);
    dlg_btn(row, "Thoát", true);
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    fd_deck_init();
    lv_obj_set_scrollable(s_body, false);
    cfg_load();
    show_setup();
}

static void destroy(void)
{
    stop_timer();
    stop_poll();
    dlg_close();
    fd_study_finish(false);   /* roi man: phien do dang giu .part, phien chua lam tra lai cache */
    s_body = s_ipa = NULL;
    for (int i = 0; i < 4; i++) s_ans[i] = s_lbl[i] = NULL;
}

static bool on_back(void)
{
    if (s_dlg) { dlg_close(); return true; }
    if (s_view == V_QUIZ) {
        stop_timer();
        dlg_open();
        return true;
    }
    if (s_view == V_LOAD) {
        if (s_wait) { s_wait = false; quit_quiz(); return true; }
        fd_study_cancel();
        show_setup();
        return true;
    }
    if (s_view != V_SETUP) { show_setup(); return true; }
    return false;   /* SETUP -> Home */
}

/* Nut cung: nut -> mau phim -> o (COLOUR_CELL[xoay]) = lua chon A..D. RESULT: bam ngan = "Hoc tiep". */
static bool on_button(const fd_evt_button_t *b)
{
    if (b->action != FD_BTN_CLICK) return false;
    if (s_view == V_QUIZ) {
        int cell = ui_key_cell(b->index);
        if (cell >= 0) answer(cell);
        return true;
    }
    if (s_view == V_RESULT && !s_dlg) {
        again_cb(NULL);
        return true;
    }
    return false;
}

const ui_screen_t g_scr_study = {
    .name = "quiz", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_back = on_back,
    .on_button = on_button,
};
