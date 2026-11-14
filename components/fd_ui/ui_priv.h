#pragma once
/* Noi bo fd_ui: moi man hinh la mot bang ham. Moi ham duoc goi khi DANG GIU lock LVGL.
 * Khong co thanh tieu de: dieu huong bang vuot mep (trai/phai = quay lai, duoi len = Home) + giu B1. */
#include <stdbool.h>
#include <stddef.h>
#include "fd_events.h"
#include "fd_ui.h"
#include "fd_i18n.h"   /* _("...") = chuoi giao dien theo ngon ngu (lv_i18n) */
#include "fd_voice.h"
#include "lvgl.h"
#include "ui_theme.h"

typedef struct {
    const char *name;
    fd_scr_t parent;                              /* man cha khi quay lai (vuot mep / giu B1) */
    void (*build)(lv_obj_t *scr);                 /* dung giao dien tren screen moi (do phan giai hien tai) */
    void (*destroy)(void);                        /* don timer / layer_top truoc khi xoa screen */
    void (*on_event)(int32_t id, void *data);     /* su kien FD_EVENT (tru nut) */
    bool (*on_button)(const fd_evt_button_t *b);  /* true = da xu ly */
    void (*on_tick)(void);                        /* moi giay */
    bool (*on_back)(void);                        /* quay lai: true = da xu ly (vd dong panel) */
} ui_screen_t;

extern const ui_screen_t g_scr_home, g_scr_settings, g_scr_wifi,
    g_scr_about, g_scr_rotation, g_scr_brightness, g_scr_volume, g_scr_timeout, g_scr_flashcard, g_scr_practice, g_scr_sync, g_scr_stats, g_scr_pin, g_scr_pair, g_scr_lang, g_scr_sentence, g_scr_mictest,
    g_scr_devmode, g_scr_btntest, g_scr_pos, g_scr_cloze, g_scr_dict, g_scr_warranty, g_scr_phone_qr, g_scr_deck, g_scr_listen;

/* Ma thiet bi: MAC eFuse 12 hex (same stable ID used in pairing QR). */
void ui_device_ident(char *out, size_t n);
/* Avatar tron chu cai dau cua ten (scr_pair.c); bubble su kien len cha. */
lv_obj_t *ui_avatar_initials(lv_obj_t *parent, const char *name, int px, uint32_t bg);

/* Nen + font mac dinh cho screen, tra ve vung noi dung toan man (flex column, pad 6). */
lv_obj_t *ui_page_create(lv_obj_t *scr);
/* Bieu tuong o khoa nho (ve bang obj, font khong co glyph khoa) */
lv_obj_t *ui_lock_icon(lv_obj_t *parent, lv_color_t color);
/* Quay lai: hoi man hien tai (on_back) truoc, neu khong thi ve man cha. Goi trong task LVGL. */
void ui_nav_back(void);
void ui_nav_home(void);
/* Xoay sang idx 0..3 (luu NVS) roi dung lai man hien tai. Goi trong task LVGL. */
void ui_nav_set_rotation(int idx);
/* Dung lai man hien tai (vd sau khi doi ngon ngu). Goi trong task LVGL. */
void ui_nav_refresh(void);
/* Bat/tat che do nha phat trien (luu NVS fd/devmode, log "devmode: on/off"). */
void ui_devmode_set(bool on);

/* scr_practice.c: nut cung -> mau phim (KEY_GPIO_COLOUR) -> o (COLOUR_CELL theo xoay hien tai). */
int ui_key_colour(int idx);            /* 0 trang, 1 cam, 2 xanh la, 3 do; -1 neu sai */
int ui_key_cell(int idx);              /* 0 TL, 1 TR, 2 BL, 3 BR; -1 neu sai */
const char *ui_key_colour_name(int k);
int ui_cell_colour(int cell);         /* o 0..3 -> mau phim theo xoay hien tai */
uint32_t ui_key_rgb(int k);           /* mau phim -> RGB vien/cham o */
uint32_t ui_key_glyph(int k);         /* mau chu OK/X trong cham */

/* ui_widgets.c: DINH DANG CHUNG o dap an (trac nghiem + moi man hoc tap co dap an):
 * nen sang trung tinh, vien 3 px mau phim, ben trai cham tron DAC mau phim (khong vien/outline/bong), roi chu toi.
 * Tra ve nut (lv_button, hang flex); *out_label = nhan chu dap an. */
lv_obj_t *ui_answer_cell(lv_obj_t *parent, uint32_t key_colour, const char *text, lv_obj_t **out_label);
typedef enum { UI_ANS_CORRECT, UI_ANS_WRONG, UI_ANS_OTHER } ui_ans_mark_t;
/* 4.1.9: o dap an kieu A/B/C/D (Luyen tap, Phan loai tu, Dien tu): mot mau, nhan "A. ...", khong cham tron.
 * cell 0..3 = TL A, TR B, BL C, BR D; nut vat ly 1..4 -> A..D (ui_abcd_button). */
lv_obj_t *ui_abcd_cell(lv_obj_t *parent, int cell, const char *text, lv_obj_t **out_label);
void ui_abcd_mark(lv_obj_t *cell, lv_obj_t *label, ui_ans_mark_t m);
int ui_abcd_button(int btn_index);   /* -1 neu ngoai 0..3 */
void ui_answer_cell_mark(lv_obj_t *cell, lv_obj_t *label, ui_ans_mark_t m, uint32_t glyph_colour);   /* OK/X trong cham mau */
/* Hien/an overlay vung vuot mep theo che do nha phat trien (goi trong task LVGL). */
void ui_swipe_overlay_sync(void);
/* Bang cai dat nhanh (ui_quick.c) */
const ui_screen_t *ui_nav_current(void);
/* ui_splash.c: man chao logo 1 s luc khoi dong / thuc deep sleep (khong khi chi bat den nen) */
void ui_splash_show(void);
bool ui_splash_active(void);
void ui_pin_wake_init(void);     /* scr_pin.c: doc co fd/pin_req luc khoi dong */
bool ui_pin_wake_locked(void);   /* true = dang khoa sau khi thuc (chi cho man PIN) */
void fd_sleep_enter(void);   /* fd_sleep.c: ngu (deep sleep), nut bat ky de bat lai */
void ui_quick_open(void);
void ui_quick_close(bool anim);
bool ui_quick_is_open(void);
bool ui_quick_hit(const lv_point_t *p);
/* Thoi gian cho man hinh (giay, 0 = khong tat), NVS fd/scr_to */
int ui_sleep_get_timeout(void);
void ui_sleep_set_timeout(int sec);
/* "HH:MM" theo gio dia phuong, "--:--" neu chua dong bo SNTP */
void ui_fmt_clock(char *out, size_t len, bool seconds);
/* Mau icon WiFi theo trang thai (xanh = da ket noi, vang = dang ket noi, xam = chua) */
lv_color_t ui_wifi_color(void);
/* Tieu de man hinh: font 32 px co dau (fd_font_noto_32_title, chi co glyph cac tieu de dung), can giua, rong 100%. */
lv_obj_t *ui_screen_title(lv_obj_t *parent, const char *txt);
/* Phan hoi doi am luong: 'bip bip' (2 x 70 ms, nghi 60 ms) qua fd_audio_play_pcm, khong chan; bo qua khi am luong 0. */
void ui_vol_beep(void);
/* Phat am tu tieng Anh (fd_pron_say, dung chung Flash card / Hoc tap / Phat am); loi xep hang -> bip, dang phat -> bo qua. */
void ui_pron_say(const char *word);
void ui_pron_say_cfg(const char *word, const fd_pron_cfg_t *cfg);   /* Hoc tap: tuy chon nguon/lap/tai */
/* scr_debug.c: dang ky bo dem 'mat Wi-Fi khi dang phat' (goi 1 lan luc khoi dong); dem so lan phat qua UI. */
/* Gach cheo 'Wi-Fi off' phu len nhan LV_SYMBOL_WIFI (Montserrat 14 khong co glyph wifi-off). off=true: hien gach cung mau chu. */
void ui_wifi_slash_sync(lv_obj_t *icon, bool off);
/* Bieu tuong pin, % nam giua than pin (Noto 14), an khi fd_batt_get_pct() < 0 (tru che do luon hien: "--"). Goi trong ngu canh LVGL. */
lv_obj_t *ui_batt_label_create(lv_obj_t *parent);
void ui_batt_label_update(lv_obj_t *lbl);
/* always=true: khong bao gio an; pct<0 -> icon pin rong mau xam + "--%". Tu cap nhat moi 1 s qua lv_timer. */
lv_obj_t *ui_batt_label_create_ex(lv_obj_t *parent, bool always);
/* 2.3.0 (ui_widgets.c): cong chua ghep dung chung cho moi mo-dun hoc: "Can ghep thiet bi de hoc" + [Quay lai][Ghep thiet bi].
 * Xoa noi dung body. Nut kieu chung (primary = nen amber). */
void ui_pair_gate(lv_obj_t *body);

/* 2.3.6: mau man cai dat truoc khi hoc DUNG CHUNG (Flash card, Luyen tap, Sap xep cau, Phan loai tu, Dien tu).
 * ui_setup_begin: dat bo cuc body (cot, khong cuon) + tieu de; tra ve vung cuon chua cac hang lua chon.
 * ui_setup_opt_row: [nhan] [nut 1][nut 2]..., user_data nut = (grp << 4) | i; opts la khoa i18n.
 * ui_setup_start: nut [Bat dau] gon (36 px, ~60% rong, giua) ghim day man. */
#define UI_OPT_WIDE 1u   /* nhan dai (Tu dong phat...) */
#define UI_OPT_WRAP 2u   /* chu trong nut xuong dong (doc) */
lv_obj_t *ui_setup_begin(lv_obj_t *body, const char *title);
void ui_setup_opt_row(lv_obj_t *sc, const char *name, const char *const *opts, int n, int sel,
                      lv_event_cb_t cb, int grp, unsigned flags);
lv_obj_t *ui_setup_start(lv_obj_t *body, lv_event_cb_t cb);
/* hang Bo tu tren man cho: nhan + gia tri hien tai, cham de mo man cau hinh Bo tu. */
void ui_setup_deck(lv_obj_t *sc, void (*refresh)(void *));
lv_obj_t *ui_gate_btn(lv_obj_t *p, const char *t, bool primary, lv_event_cb_t cb);
