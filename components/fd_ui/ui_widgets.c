#include "esp_attr.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "esp_timer.h"
#include "fd_audio.h"
#include "fd_batt.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "fd_voice.h"
#include "ui_priv.h"
#include "fd_tts.h"

LV_FONT_DECLARE(fd_font_batt_11);   /* fd_font_batt_11.c: Noto Sans 11 px, chi "0123456789%-" */

void ui_fmt_clock(char *out, size_t len, bool seconds)
{
    if (!fd_net_time_valid()) {
        snprintf(out, len, seconds ? "--:--:--" : "--:--");
        return;
    }
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(out, len, seconds ? "%H:%M:%S" : "%H:%M", &lt);
}

lv_obj_t *ui_screen_title(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *hdr = lv_label_create(parent);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_style_text_font(hdr, &fd_font_noto_32_title, 0);
    lv_obj_set_style_text_align(hdr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x111827), 0);
    lv_label_set_text(hdr, txt);
    return hdr;
}

/* 'bip bip' doi am luong: PCM 16 kHz mono tao 1 lan (bo dem tinh, ton tai suot khi phat). Am luong do codec ap dung. */
#define VB_RATE   16000
#define VB_TONE   (VB_RATE * 70 / 1000)
#define VB_GAP    (VB_RATE * 60 / 1000)
#define VB_RAMP   (VB_RATE * 5 / 1000)
static EXT_RAM_BSS_ATTR int16_t s_vb_pcm[2 * VB_TONE + VB_GAP];
static bool s_vb_ready;

void ui_vol_beep(void)
{
    if (fd_audio_get_volume() <= 0) return;
    if (!s_vb_ready) {   /* tao 1 lan; khong ghi lai bo dem khi task am thanh dang doc */
        for (int i = 0; i < VB_TONE; i++) {
            float env = 1.0f;
            if (i < VB_RAMP) env = (float)i / VB_RAMP;
            else if (i > VB_TONE - VB_RAMP) env = (float)(VB_TONE - i) / VB_RAMP;
            int16_t s = (int16_t)(12000.0f * env * sinf(2.0f * (float)M_PI * 1200.0f * i / VB_RATE));
            s_vb_pcm[i] = s;
            s_vb_pcm[VB_TONE + VB_GAP + i] = s;
        }
        s_vb_ready = true;
    }
    fd_audio_play_pcm(s_vb_pcm, sizeof(s_vb_pcm) / sizeof(s_vb_pcm[0]));
}

void ui_pron_say(const char *word) { ui_pron_say_cfg(word, NULL); }

void ui_pron_say_cfg(const char *word, const fd_pron_cfg_t *cfg)
{
    esp_err_t r = fd_pron_say_cfg(word, cfg);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) {
        if (!fd_tts_voice_ok()) fd_ui_toast(_("VOICE_DATA_MISSING"));   /* 23/10: thieu phan vung voice */
        else ui_vol_beep();
    }
}

lv_color_t ui_wifi_color(void)
{
    if (fd_net_is_connected()) return lv_palette_main(LV_PALETTE_GREEN);
    if (fd_net_state() == FD_NET_CONNECTING) return lv_palette_main(LV_PALETTE_AMBER);
    return lv_palette_main(LV_PALETTE_GREY);
}

/* Gach cheo 2 px tu goc tren-trai den duoi-phai cua glyph wifi Montserrat 14 (~18x14), la con thu 0 cua nhan icon */
void ui_wifi_slash_sync(lv_obj_t *icon, bool off)
{
    static const lv_point_precise_t pts[] = {{1, 1}, {18, 14}};
    if (!icon) return;
    lv_obj_t *ln = lv_obj_get_child_count(icon) ? lv_obj_get_child(icon, 0) : NULL;
    if (!ln) {
        if (!off) return;
        ln = lv_line_create(icon);
        lv_line_set_points(ln, pts, 2);
        lv_obj_set_style_line_width(ln, 2, 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_set_clickable(ln, false);
    }
    lv_obj_set_style_line_color(ln, lv_obj_get_style_text_color(icon, LV_PART_MAIN), 0);
    lv_obj_set_hidden(ln, !off);
}

/* Bieu tuong pin ve bang doi tuong LVGL, % nam GIUA than pin (19/10):
 * goc r (35x14, trong suot) = than (32x14, vien 1 px, bo 2) + num cuc (2x6, ben phai). "100%" = 27 px < 30 px trong.
 * Trong than: thanh day (rong theo %, xanh / do < 20%, mo 50%) + nhan % Noto Sans 11 px (fd_font_batt_11) mau toi o giua.
 * Chua doc duoc: "--", vien xam, khong co thanh day. */
#define BATT_W      32
#define BATT_H      14
#define BATT_INNER  (BATT_W - 2 * 1)   /* tru vien 1 px moi ben, pad 0 */

void ui_batt_label_update(lv_obj_t *r)
{
    if (!r) return;
    lv_obj_t *body = lv_obj_get_child(r, 0), *nub = lv_obj_get_child(r, 1);
    lv_obj_t *fill = lv_obj_get_child(body, 0), *lbl = lv_obj_get_child(body, 1);
    int p = fd_batt_get_pct();
    if (p < 0) {
        if (!lv_obj_get_user_data(r)) {   /* khong o che do luon hien -> an */
            lv_obj_set_hidden(r, true);
            return;
        }
        lv_color_t g = lv_palette_main(LV_PALETTE_GREY);
        lv_obj_set_style_border_color(body, g, 0);
        lv_obj_set_style_bg_color(nub, g, 0);
        lv_obj_set_hidden(fill, true);
        lv_label_set_text(lbl, "--");
        lv_obj_set_style_text_color(lbl, g, 0);
        lv_obj_set_hidden(r, false);
        return;
    }
    if (p > 100) p = 100;
    bool low = p < 20;
    lv_color_t edge = low ? lv_palette_main(LV_PALETTE_RED) : lv_color_hex(0x4B5563);
    lv_obj_set_style_border_color(body, edge, 0);
    lv_obj_set_style_bg_color(nub, edge, 0);
    lv_obj_set_style_bg_color(fill, low ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_width(fill, LV_MAX(1, BATT_INNER * p / 100));
    lv_obj_set_hidden(fill, false);
    lv_label_set_text_fmt(lbl, "%d%%", p);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x111827), 0);
    lv_obj_set_hidden(r, false);
}

static void batt_tmr_cb(lv_timer_t *t) { ui_batt_label_update((lv_obj_t *)lv_timer_get_user_data(t)); }
static void batt_del_cb(lv_event_t *e) { lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e)); }

lv_obj_t *ui_batt_label_create_ex(lv_obj_t *parent, bool always)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, BATT_W + 3, BATT_H);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_clickable(r, false);
    lv_obj_set_user_data(r, always ? (void *)1 : NULL);

    lv_obj_t *body = lv_obj_create(r);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, BATT_W, BATT_H);
    lv_obj_align(body, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_clickable(body, false);

    lv_obj_t *fill = lv_obj_create(body);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 1, lv_pct(100));
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(fill, 1, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_50, 0);
    lv_obj_set_clickable(fill, false);

    lv_obj_t *lbl = lv_label_create(body);
    lv_obj_set_style_text_font(lbl, &fd_font_batt_11, 0);   /* chi so + "%-", Noto Sans 11 px */
    lv_obj_center(lbl);

    lv_obj_t *nub = lv_obj_create(r);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 2, 6);
    lv_obj_align(nub, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(nub, 1, 0);
    lv_obj_set_clickable(nub, false);

    ui_batt_label_update(r);
    if (always) {
        lv_timer_t *t = lv_timer_create(batt_tmr_cb, 1000, r);
        lv_obj_add_event_cb(r, batt_del_cb, LV_EVENT_DELETE, t);
    }
    return r;
}

lv_obj_t *ui_batt_label_create(lv_obj_t *parent) { return ui_batt_label_create_ex(parent, false); }

lv_obj_t *ui_page_create(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF4F6F8), 0);
    lv_obj_set_style_text_font(scr, &fd_font_noto_14, 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    return body;
}

lv_obj_t *ui_lock_icon(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *w = lv_obj_create(parent);
    lv_obj_remove_style_all(w);
    lv_obj_set_size(w, 10, 13);
    lv_obj_t *shackle = lv_obj_create(w);
    lv_obj_remove_style_all(shackle);
    lv_obj_set_size(shackle, 8, 9);
    lv_obj_set_pos(shackle, 1, 0);
    lv_obj_set_style_border_width(shackle, 2, 0);
    lv_obj_set_style_border_color(shackle, color, 0);
    lv_obj_set_style_radius(shackle, 4, 0);
    lv_obj_t *body = lv_obj_create(w);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 10, 7);
    lv_obj_set_pos(body, 0, 6);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(body, color, 0);
    lv_obj_set_style_radius(body, 1, 0);
    return w;
}

/* ---------- O dap an (dinh dang chung, 20/10 09:28) ---------- */
#define ANS_BG      0xF3F4F6   /* xam rat nhat (09:30): vien/cham hong pastel van thay tren nen */
#define ANS_TEXT    0x111827

lv_obj_t *ui_answer_cell(lv_obj_t *parent, uint32_t key_colour, const char *text, lv_obj_t **out_label)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ANS_BG), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xE5E7EB), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0);   /* vien mong 1 px mau phim, dong nhat moi o, ca khi phan hoi */
    lv_obj_set_style_border_color(b, lv_color_hex(key_colour), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_pad_ver(b, 2, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_t *dot = lv_obj_create(b);   /* cham tron dac: lv_obj thuong, khong phai ky tu/radio */
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 20, 20);   /* du cho OK/X khi phan hoi */
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(key_colour), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_outline_width(dot, 0, 0);
    lv_obj_set_style_shadow_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_clickable(dot, false);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_height(l, 40);   /* toi da 2 dong, cat "..." */
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(l, lv_color_hex(ANS_TEXT), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(l, text ? text : "");
    if (out_label) *out_label = l;
    return b;
}

void ui_answer_cell_mark(lv_obj_t *cell, lv_obj_t *label, ui_ans_mark_t m, uint32_t glyph_colour)
{
    if (!cell) return;
    lv_obj_set_clickable(cell, false);
    if (m == UI_ANS_OTHER) {
        lv_obj_set_style_opa(cell, LV_OPA_40, 0);
        return;
    }
    if (m == UI_ANS_WRONG) {   /* vien giu 1 px; sai: mo + gach chu */
        lv_obj_set_style_opa(cell, LV_OPA_60, 0);
        if (label) lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
    }
    /* OK / X nam GIUA cham mau ben trai (con 0 cua o), khong co huy hieu rieng. Montserrat 14 co san LV_SYMBOL_OK/CLOSE. */
    lv_obj_t *dot = lv_obj_get_child(cell, 0);
    if (!dot) return;
    lv_obj_t *ic = lv_label_create(dot);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(glyph_colour), 0);
    lv_label_set_text(ic, m == UI_ANS_CORRECT ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_center(ic);
}

lv_obj_t *ui_abcd_cell(lv_obj_t *parent, int cell, const char *text, lv_obj_t **out_label)
{
    char txt[200];
    snprintf(txt, sizeof(txt), "%c. %s", 'A' + (cell & 3), text ? text : "");
    lv_obj_t *b = ui_answer_cell(parent, FD_COLOR_PRIMARY, txt, out_label);
    lv_obj_t *dot = lv_obj_get_child(b, 0);   /* khong cham tron: an cho toi khi phan hoi */
    if (dot) { lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0); lv_obj_set_width(dot, 0); }
    return b;
}

void ui_abcd_mark(lv_obj_t *cell, lv_obj_t *label, ui_ans_mark_t m)
{
    if (!cell) return;
    lv_obj_t *dot = lv_obj_get_child(cell, 0);
    if (dot && m != UI_ANS_OTHER) lv_obj_set_width(dot, 20);   /* cho cho dau OK/X */
    ui_answer_cell_mark(cell, label, m, m == UI_ANS_CORRECT ? 0x16A34A : 0xDC2626);
}

int ui_abcd_button(int btn_index) { return (btn_index >= 0 && btn_index < 4) ? btn_index : -1; }

/* 2.3.5: cong "Dang nhap de tiep tuc" dung chung cho MOI mo-dun hoc: chi kiem tra token cuc bo (buoc dang nhap = ghep thiet bi),
 * da dang nhap thi hoc offline tu kho dong bo. */
static void gate_home_cb(lv_event_t *e) { (void)e; ui_nav_home(); }
static void gate_pair_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_PAIR); }

lv_obj_t *ui_gate_btn(lv_obj_t *p, const char *t, bool primary, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_height(b, 36);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(primary ? 0xFFFFFF : FD_COLOR_TEXT), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *gate_label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, t);
    return l;
}

void ui_pair_gate(lv_obj_t *body)
{
    lv_obj_clean(body);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 8, 0);
    gate_label(body, _("PAIR_TO_STUDY"), &fd_font_noto_18, FD_COLOR_TEXT);
    gate_label(body, _("PAIR_HINT"), NULL, FD_COLOR_TEXT_MUTED);
    lv_obj_t *r = lv_obj_create(body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 44);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    ui_gate_btn(r, _("BACK"), false, gate_home_cb);
    ui_gate_btn(r, _("PAIR_STEP_ID"), true, gate_pair_cb);
}

/* ---------- 2.3.6: mau man cai dat truoc khi hoc (dung chung moi module) ---------- */
static bool setup_portrait(void)
{
    return lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL);
}

lv_obj_t *ui_setup_begin(lv_obj_t *body, const char *title)
{
    lv_obj_set_scrollable(body, false);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 4, 0);
    lv_obj_t *sc = lv_obj_create(body);
    lv_obj_remove_style_all(sc);
    lv_obj_set_width(sc, lv_pct(100));
    lv_obj_set_flex_grow(sc, 1);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sc, 6, 0);
    lv_obj_set_style_pad_bottom(sc, 4, 0);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *t = lv_label_create(sc);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_height(t, 26);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_label_set_text(t, title ? title : "");
    return sc;
}

void ui_setup_opt_row(lv_obj_t *sc, const char *name, const char *const *opts, int n, int sel,
                      lv_event_cb_t cb, int grp, unsigned flags)
{
    bool por = setup_portrait();
    int32_t bh = por ? 36 : 34;
    lv_obj_t *r = lv_obj_create(sc);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_t *l = lv_label_create(r);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(l, name ? name : "");
    if (flags & UI_OPT_WIDE) {
        lv_obj_set_width(l, por ? 56 : 92);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(l, -3, 0);
    } else lv_obj_set_width(l, por ? 48 : 62);
    for (int i = 0; i < n; i++) {
        bool on = i == sel;
        lv_obj_t *b = lv_button_create(r);
        lv_obj_set_height(b, bh);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
        lv_obj_set_style_border_width(b, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_pad_hor(b, 2, 0);
        lv_obj_t *bl = lv_label_create(b);
        lv_obj_set_style_text_color(bl, on ? lv_color_white() : lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_text(bl, _(opts[i]));
        lv_obj_set_width(bl, lv_pct(100));
        lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
        if ((flags & UI_OPT_WRAP) && por) {
            lv_label_set_long_mode(bl, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_line_space(bl, -3, 0);
        } else lv_label_set_long_mode(bl, LV_LABEL_LONG_DOT);
        lv_obj_center(bl);
        if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)((grp << 4) | i));
    }
}

lv_obj_t *ui_setup_start(lv_obj_t *body, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, lv_pct(60), 36);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, _("START"));
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}
/* 4.3.3: hang Bo tu tren man cho 5 module, cung kieu cac hang lua chon ben duoi.
 * Ba nut (Tat ca / Tu moi them / Ket hop) mot hang; khi Ket hop, cac bo tu hien o hang sau, xep ngang. */
static void (*s_deck_refresh)(void *);

static void deck_kick_refresh(void)
{
    if (s_deck_refresh) lv_async_call(s_deck_refresh, NULL);
}

static void deck_mode_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e) & 0xF;
    fd_sync_pick_set_mode((fd_sync_pick_mode_t)i);
    deck_kick_refresh();
}

static void deck_col_cb(lv_event_t *e)
{
    fd_sync_pick_toggle_col((int)(intptr_t)lv_event_get_user_data(e));
    deck_kick_refresh();
}

static void deck_check_mark(lv_obj_t *b, bool on)
{
    lv_obj_t *o = lv_obj_create(b);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 18, 18);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_border_width(o, on ? 0 : 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(o, on ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    if (on) {
        lv_obj_t *l = lv_label_create(o);
        lv_obj_set_style_text_font(l, &fd_font_mont_24, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text(l, LV_SYMBOL_OK);
        lv_obj_center(l);
    }
}

void ui_setup_deck(lv_obj_t *sc, void (*refresh)(void *))
{
    s_deck_refresh = refresh;
    static const char *const MD[] = {"ALL", "NEW_ADDED", "COMBINED"};
    fd_sync_pick_t p;
    fd_sync_pick_get(&p);
    int md = (int)p.mode;
    if (md < 0 || md > 2) md = 0;
    ui_setup_opt_row(sc, _("DECK"), MD, 3, md, deck_mode_cb, 0, UI_OPT_WRAP);

    if (md != (int)FD_SYNC_PICK_COMBINED) return;

    bool por = setup_portrait();
    lv_obj_t *row = lv_obj_create(sc);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_scrollable(row, false);

    lv_obj_t *sp = lv_obj_create(row);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, por ? 48 : 62, 1);
    lv_obj_set_clickable(sp, false);
    lv_obj_set_scrollable(sp, false);

    lv_obj_t *chips = lv_obj_create(row);
    lv_obj_remove_style_all(chips);
    lv_obj_set_flex_grow(chips, 1);
    lv_obj_set_width(chips, 1);
    lv_obj_set_height(chips, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(chips, 6, 0);
    lv_obj_set_style_pad_row(chips, 6, 0);
    lv_obj_set_scrollable(chips, false);

    int nc = fd_sync_col_count();
    int32_t bh = por ? 36 : 34;
    for (int i = 0; i < nc; i++) {
        fd_sync_col_t c;
        if (!fd_sync_col_get(i, &c)) continue;
        bool on = fd_sync_pick_col_on(i);
        lv_obj_t *b = lv_button_create(chips);
        lv_obj_set_height(b, bh);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
        lv_obj_set_style_border_width(b, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 6, 0);
        deck_check_mark(b, on);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(l, on ? lv_color_white() : lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_text(l, c.name[0] ? c.name : c.id);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, 88);
        lv_obj_add_event_cb(b, deck_col_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}
