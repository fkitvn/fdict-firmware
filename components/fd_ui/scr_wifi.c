/*
 * Cai dat > WiFi: trang thai, quet (RSSI + khoa), nhap mat khau bang ban phim T9 (fd_t9kb) (an/hien),
 * ket noi mang moi / mang da luu, quen mang. Sai mat khau bao loi ro rang.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "ui_priv.h"
#include "fd_t9kb.h"

static const char *TAG = "scr_wifi";

static lv_obj_t *s_scr;
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_scan_btn_lbl;
static lv_obj_t *s_panel;       /* lop nhap mat khau / thao tac mang */
static lv_obj_t *s_ta;
static lv_obj_t *s_eye_lbl;
static char s_sel_ssid[33];
static bool s_sel_secure;
static fd_net_ap_t s_aps[FD_NET_MAX_SCAN];
static int s_ap_n;

static void set_status(const char *txt, lv_color_t color)
{
    if (!s_status) return;
    lv_label_set_text(s_status, txt);
    lv_obj_set_style_text_color(s_status, color, 0);
}

static void status_from_state(void)
{
    char ssid[33], ip[16], buf[96];
    fd_net_ssid(ssid, sizeof(ssid));
    fd_net_ip(ip, sizeof(ip));
    switch (fd_net_state()) {
    case FD_NET_CONNECTED:
        snprintf(buf, sizeof(buf), "Đã kết nối: %s (%d dBm)\nIP %s", ssid, fd_net_rssi(), ip);
        set_status(buf, lv_palette_darken(LV_PALETTE_GREEN, 2));
        break;
    case FD_NET_CONNECTING:
        snprintf(buf, sizeof(buf), "Đang kết nối %s…", ssid);
        set_status(buf, lv_palette_darken(LV_PALETTE_AMBER, 3));
        break;
    case FD_NET_WAIT_RETRY:
        set_status("Mất kết nối – đang thử lại…", lv_palette_darken(LV_PALETTE_AMBER, 3));
        break;
    default:
        set_status(fd_net_saved_count() ? "Chưa kết nối" : "Chưa có mạng nào được lưu. Chọn mạng bên dưới.",
                   lv_palette_darken(LV_PALETTE_GREY, 2));
        break;
    }
}

/* ---------- panel nhap mat khau ---------- */
static void panel_close(void)
{
    if (s_panel) {
        lv_obj_delete(s_panel);
        s_panel = NULL;
        s_ta = NULL;
        s_eye_lbl = NULL;
    }
}

static void do_connect(void)
{
    const char *pass = s_ta ? lv_textarea_get_text(s_ta) : "";
    if (s_sel_secure && strlen(pass) < 8) {
        set_status("Mật khẩu WPA cần ít nhất 8 ký tự", lv_palette_main(LV_PALETTE_RED));
        return;
    }
    ESP_LOGI(TAG, "Ket noi \"%s\" (mat khau %u ky tu)", s_sel_ssid, (unsigned)strlen(pass));
    fd_net_connect(s_sel_ssid, pass);
    panel_close();
    status_from_state();
}

static void connect_cb(lv_event_t *e) { (void)e; do_connect(); }
static void cancel_cb(lv_event_t *e) { (void)e; panel_close(); }

/* Phim Xong tren ban phim T9 -> ket noi (do_connect tu doc textarea va dong panel) */
static void t9_done_cb(lv_obj_t *kb, const char *text, void *user_data)
{
    (void)kb; (void)text; (void)user_data;
    do_connect();
}

static void eye_cb(lv_event_t *e)
{
    (void)e;
    bool hidden = lv_textarea_get_password_mode(s_ta);
    lv_textarea_set_password_mode(s_ta, !hidden);
    lv_label_set_text(s_eye_lbl, hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);   /* mo = dang hien, nham = dang an */
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    fd_net_forget(s_sel_ssid);
    panel_close();
    status_from_state();
    fd_net_scan_start();
}

static void saved_connect_cb(lv_event_t *e)
{
    (void)e;
    fd_net_connect_saved(s_sel_ssid);
    panel_close();
    status_from_state();
}

static lv_obj_t *small_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, lv_color_t bg)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 30);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *panel_base(void)
{
    s_panel = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, lv_pct(100), lv_pct(100));   /* khong header: panel phu toan man */
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0xF4F6F8), 0);
    lv_obj_set_style_pad_all(s_panel, 4, 0);
    lv_obj_set_style_pad_row(s_panel, 2, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(s_panel, false);
    lv_obj_t *t = lv_label_create(s_panel);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(t, "Mạng: %s", s_sel_ssid);
    return s_panel;
}

static void open_password_panel(bool saved)
{
    panel_close();
    lv_obj_t *p = panel_base();

    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 30);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);

    s_ta = lv_textarea_create(row);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_password_mode(s_ta, false);   /* mac dinh hien mat khau moi lan mo panel */
    lv_textarea_set_max_length(s_ta, 63);   /* WPA2 passphrase 8..63 */
    lv_textarea_set_placeholder_text(s_ta, "Mật khẩu");
    lv_obj_set_flex_grow(s_ta, 1);
    lv_obj_set_height(s_ta, 30);
    lv_obj_set_style_pad_ver(s_ta, 5, 0);
    lv_obj_set_style_text_font(s_ta, &fd_font_noto_14, 0);

    lv_obj_t *eye = lv_button_create(row);
    lv_obj_set_size(eye, 36, 30);
    s_eye_lbl = lv_label_create(eye);
    lv_obj_set_style_text_font(s_eye_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_eye_lbl, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(s_eye_lbl);
    lv_obj_add_event_cb(eye, eye_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cb = small_btn(row, "Kết nối", connect_cb, lv_color_hex(FD_COLOR_PRIMARY));
    lv_obj_set_style_pad_hor(cb, 14, 0);   /* nut X bo: Ket noi rong hon (huy = vuot mep / giu B1) */
    if (saved) {
        small_btn(row, "Quên", forget_cb, lv_palette_main(LV_PALETTE_RED));
    }

    /* Ban phim T9 (phim lon ~59x48) thay lv_keyboard */
    lv_obj_t *kb = fd_t9kb_create(p, s_ta, t9_done_cb, NULL);
    lv_obj_set_flex_grow(kb, 1);
}

static void change_pw_cb(lv_event_t *e)
{
    (void)e;
    open_password_panel(true);
}

/* ---------- panel mang da luu: sap theo muc do quan trong ----------
 * SSID + trang thai / [Ket noi lai] (chinh, amber, rong) / [Doi mat khau][Dong] (phu) /
 * vach mong / ... / [Quen mang nay] (nguy hiem, vien do, tach xuong day, co hoi xac nhan) */
static lv_obj_t *s_fdlg;   /* hop thoai xac nhan quen mang (layer_top) */

static void fdlg_close(void)
{
    if (s_fdlg) {
        lv_obj_delete(s_fdlg);
        s_fdlg = NULL;
    }
}

static void fdlg_btn_cb(lv_event_t *e)
{
    bool ok = (bool)(intptr_t)lv_event_get_user_data(e);
    fdlg_close();
    if (ok) forget_cb(e);
}

static void fdlg_btn(lv_obj_t *row, const char *txt, bool ok)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, ok ? lv_color_white() : lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_border_width(b, ok ? 2 : 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, ok ? lv_color_hex(0xDC2626) : lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, fdlg_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ok);
}

static void forget_ask_cb(lv_event_t *e)
{
    (void)e;
    if (s_fdlg) return;
    s_fdlg = lv_obj_create(lv_layer_top());   /* cung kieu hop thoai devmode */
    lv_obj_remove_style_all(s_fdlg);
    lv_obj_set_size(s_fdlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_fdlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_fdlg, LV_OPA_40, 0);
    lv_obj_set_clickable(s_fdlg, true);
    lv_obj_t *box = lv_obj_create(s_fdlg);
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
    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(t, "Quên mạng %s?", s_sel_ssid);
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    fdlg_btn(row, "Hủy", false);
    fdlg_btn(row, "Quên", true);
}

typedef enum { BTN_PRIMARY, BTN_SECONDARY, BTN_DANGER } btn_kind_t;

static lv_obj_t *act_btn(lv_obj_t *parent, const char *sym, const char *txt, lv_event_cb_t cb, btn_kind_t k)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 40);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_color_t fg = lv_color_hex(k == BTN_DANGER ? 0xDC2626 : FD_COLOR_ON_PRIMARY);
    lv_obj_set_style_bg_color(b, k == BTN_PRIMARY ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
    if (k != BTN_PRIMARY) {
        lv_obj_set_style_border_width(b, k == BTN_DANGER ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, k == BTN_DANGER ? fg : lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(k == BTN_DANGER ? 0xFEE2E2 : 0xF3F4F6), LV_STATE_PRESSED);
    }
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_t *i = lv_label_create(b);   /* ky hieu: font Montserrat (Noto khong co LV_SYMBOL) */
    lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(i, fg, 0);
    lv_label_set_text(i, sym);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_label_set_text(l, txt);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void open_saved_panel(void)
{
    panel_close();
    lv_obj_t *p = panel_base();
    lv_obj_set_style_pad_all(p, 8, 0);
    lv_obj_set_style_pad_row(p, 6, 0);

    lv_obj_t *n = lv_label_create(p);
    lv_obj_set_width(n, lv_pct(100));
    lv_obj_set_style_text_font(n, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(n, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_label_set_text(n, s_sel_ssid);

    char cur[33];
    fd_net_ssid(cur, sizeof(cur));
    bool same = strcmp(cur, s_sel_ssid) == 0;
    lv_obj_t *st = lv_label_create(p);
    lv_obj_set_width(st, lv_pct(100)); lv_label_set_long_mode(st, LV_LABEL_LONG_WRAP);   /* ngang: trang thai dai xuong dong */
    if (same && fd_net_state() == FD_NET_CONNECTED) {
        lv_label_set_text(st, "Đã lưu · Đang kết nối với mạng này");
        lv_obj_set_style_text_color(st, lv_palette_darken(LV_PALETTE_GREEN, 2), 0);
    } else if (same && fd_net_state() == FD_NET_CONNECTING) {
        lv_label_set_text(st, "Đã lưu · Đang kết nối…");
        lv_obj_set_style_text_color(st, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    } else {
        lv_label_set_text(st, "Đã lưu · Chưa kết nối");
        lv_obj_set_style_text_color(st, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    }

    lv_obj_t *b = act_btn(p, LV_SYMBOL_REFRESH, "Kết nối lại", saved_connect_cb, BTN_PRIMARY);
    lv_obj_set_width(b, lv_pct(100));

    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_grow(act_btn(row, LV_SYMBOL_EDIT, "Đổi mật khẩu", change_pw_cb, BTN_SECONDARY), 1);
    lv_obj_set_flex_grow(act_btn(row, LV_SYMBOL_CLOSE, "Đóng", cancel_cb, BTN_SECONDARY), 1);

    lv_obj_t *gap = lv_obj_create(p);   /* day nut nguy hiem xuong day */
    lv_obj_remove_style_all(gap);
    lv_obj_set_width(gap, lv_pct(100));
    lv_obj_set_flex_grow(gap, 1);
    lv_obj_t *sep = lv_obj_create(p);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, lv_pct(100), 1);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(FD_COLOR_BORDER), 0);

    b = act_btn(p, LV_SYMBOL_TRASH, "Quên mạng này", forget_ask_cb, BTN_DANGER);
    lv_obj_set_width(b, lv_pct(100));
}
/* ---------- danh sach mang ---------- */
static void ap_clicked_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_ap_n) return;
    strlcpy(s_sel_ssid, s_aps[i].ssid, sizeof(s_sel_ssid));
    s_sel_secure = s_aps[i].secure;
    if (s_aps[i].saved) {
        open_saved_panel();
    } else if (s_aps[i].secure) {
        open_password_panel(false);
    } else {
        fd_net_connect(s_sel_ssid, "");
        status_from_state();
    }
}

static lv_obj_t *s_scan_spin;

static void scan_busy(bool busy)
{
    if (!s_scan_btn_lbl) return;
    lv_obj_set_hidden(s_scan_btn_lbl, busy);
    if (s_scan_spin) lv_obj_set_hidden(s_scan_spin, !busy);
}

static int ap_rank(const fd_net_ap_t *a, const char *cur, bool connected)
{
    return (connected && strcmp(cur, a->ssid) == 0) ? 2 : a->saved ? 1 : 0;
}

static void section(const char *txt)
{
    lv_obj_t *l = lv_label_create(s_list);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_style_pad_left(l, 4, 0);
    lv_obj_set_style_pad_top(l, 4, 0);
    lv_label_set_text(l, txt);
}

static void populate(void)
{
    if (!s_list) return;
    lv_obj_clean(s_list);
    int n = fd_net_scan_results(s_aps, FD_NET_MAX_SCAN);
    char cur[33];
    fd_net_ssid(cur, sizeof(cur));
    bool connected = fd_net_is_connected();

    /* bo SSID rong, gop SSID trung (giu manh nhat) */
    s_ap_n = 0;
    for (int i = 0; i < n; i++) {
        if (!s_aps[i].ssid[0]) continue;
        int k = 0;
        while (k < s_ap_n && strcmp(s_aps[k].ssid, s_aps[i].ssid) != 0) k++;
        if (k < s_ap_n) {
            if (s_aps[i].rssi > s_aps[k].rssi) s_aps[k] = s_aps[i];
            continue;
        }
        s_aps[s_ap_n++] = s_aps[i];
    }
    /* sap xep: dang ket noi > da luu > RSSI giam dan (chen, n nho) */
    for (int i = 1; i < s_ap_n; i++) {
        fd_net_ap_t t = s_aps[i];
        int rt = ap_rank(&t, cur, connected), j = i - 1;
        while (j >= 0) {
            int rj = ap_rank(&s_aps[j], cur, connected);
            if (rj > rt || (rj == rt && s_aps[j].rssi >= t.rssi)) break;
            s_aps[j + 1] = s_aps[j];
            j--;
        }
        s_aps[j + 1] = t;
    }

    if (s_ap_n == 0) {
        lv_obj_t *l = lv_label_create(s_list);
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
        lv_obj_set_style_pad_top(l, 24, 0);
        lv_label_set_text(l, fd_net_scanning() ? "Đang quét…" : "Không tìm thấy mạng nào");
        return;
    }
    bool any_saved = ap_rank(&s_aps[0], cur, connected) > 0, hdr_avail = false;
    if (any_saved) section("ĐÃ LƯU");
    for (int i = 0; i < s_ap_n; i++) {
        const fd_net_ap_t *a = &s_aps[i];
        int rank = ap_rank(a, cur, connected);
        if (rank == 0 && !hdr_avail) {
            section("MẠNG KHẢ DỤNG");
            hdr_avail = true;
        }
        lv_obj_t *b = lv_button_create(s_list);
        lv_obj_set_size(b, lv_pct(100), 44);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, lv_color_white(), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY_TINT), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_style_pad_ver(b, 2, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 8, 0);
        lv_obj_set_scroll_on_focus(b, false);

        /* song: manh > -60 amber, -60..-75 xam, < -75 xam nhat */
        lv_obj_t *w = lv_label_create(b);
        lv_obj_set_style_text_font(w, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(w, lv_color_hex(a->rssi > -60 ? FD_COLOR_PRIMARY_DARK
                                                    : a->rssi >= -75 ? FD_COLOR_TEXT_MUTED : 0xD1D5DB), 0);
        lv_label_set_text(w, LV_SYMBOL_WIFI);

        lv_obj_t *mid = lv_obj_create(b);
        lv_obj_remove_style_all(mid);
        lv_obj_set_height(mid, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(mid, 1);
        lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_clickable(mid, false);
        lv_obj_t *nm = lv_label_create(mid);
        lv_obj_set_width(nm, lv_pct(100));
        lv_obj_set_style_text_color(nm, lv_color_hex(FD_COLOR_TEXT), 0);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_label_set_text(nm, a->ssid);
        lv_obj_t *sub = lv_label_create(mid);
        lv_obj_set_width(sub, lv_pct(100)); lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(sub, lv_color_hex(rank == 2 ? 0x16A34A : FD_COLOR_TEXT_MUTED), 0);
        lv_label_set_text(sub, rank == 2 ? "Đang kết nối" : rank == 1 ? "Đã lưu" : a->secure ? "Bảo mật" : "Mở");

        if (a->secure) ui_lock_icon(b, lv_color_hex(FD_COLOR_TEXT_MUTED));
        lv_obj_t *ch = lv_label_create(b);
        lv_obj_set_style_text_font(ch, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ch, lv_color_hex(0x9CA3AF), 0);
        lv_label_set_text(ch, LV_SYMBOL_RIGHT);
        lv_obj_add_event_cb(b, ap_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}
static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (fd_net_scan_start() == ESP_OK) {
        scan_busy(true);
    }
}

static void build(lv_obj_t *scr)
{
    s_scr = scr;
    lv_obj_t *body = ui_page_create(scr);

    lv_obj_t *top = lv_obj_create(body);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 6, 0);
    s_status = lv_label_create(top);
    lv_obj_set_flex_grow(s_status, 1);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_t *sb = lv_button_create(top);   /* nut quet nho goc tren-phai, spinner khi dang quet */
    lv_obj_set_size(sb, 40, 36);
    lv_obj_set_style_radius(sb, 10, 0);
    lv_obj_set_style_shadow_width(sb, 0, 0);
    lv_obj_set_style_bg_color(sb, lv_color_white(), 0);
    lv_obj_set_style_border_width(sb, 1, 0);
    lv_obj_set_style_border_color(sb, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(sb, 0, 0);
    s_scan_btn_lbl = lv_label_create(sb);
    lv_obj_set_style_text_font(s_scan_btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_scan_btn_lbl, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_text(s_scan_btn_lbl, LV_SYMBOL_REFRESH);
    lv_obj_center(s_scan_btn_lbl);
    s_scan_spin = lv_spinner_create(sb);
    lv_obj_set_size(s_scan_spin, 22, 22);
    lv_obj_set_style_arc_width(s_scan_spin, 3, 0);
    lv_obj_set_style_arc_width(s_scan_spin, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_scan_spin, lv_color_hex(FD_COLOR_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_clickable(s_scan_spin, false);
    lv_obj_center(s_scan_spin);
    lv_obj_set_hidden(s_scan_spin, true);
    lv_obj_add_event_cb(sb, scan_cb, LV_EVENT_CLICKED, NULL);

    s_list = lv_obj_create(body);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_width(s_list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0xD1D5DB), LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    status_from_state();
    populate();
    if (fd_net_scan_start() == ESP_OK) {
        scan_busy(true);
    }
}

static void destroy(void)
{
    fdlg_close();   /* roi man khi hop thoai mo = Huy */
    s_scr = s_status = s_list = s_scan_btn_lbl = s_scan_spin = s_panel = s_ta = s_eye_lbl = NULL;
}

static void on_event(int32_t id, void *data)
{
    if (id == FD_EVT_WIFI_SCAN_DONE) {
        scan_busy(false);
        populate();
    } else if (id == FD_EVT_WIFI_DISCONNECTED) {
        const fd_evt_wifi_disc_t *d = data;
        char buf[160];
        if (d->auth_fail) {
            snprintf(buf, sizeof(buf), "Sai mật khẩu cho \"%s\". Chọn mạng để nhập lại.", d->ssid);
            set_status(buf, lv_palette_main(LV_PALETTE_RED));
        } else if (d->auth_reject) {
            snprintf(buf, sizeof(buf), "Không xác thực được với router \"%s\" (mã %d). Thử lại sau.", d->ssid, d->reason);
            set_status(buf, lv_palette_main(LV_PALETTE_RED));
        } else if (d->no_ap) {
            snprintf(buf, sizeof(buf), "Không tìm thấy mạng \"%s\"", d->ssid);
            set_status(buf, lv_palette_main(LV_PALETTE_RED));
        } else {
            status_from_state();
        }
        populate();
    } else if (id == FD_EVT_WIFI_CONNECTED || id == FD_EVT_WIFI_CONNECTING) {
        status_from_state();
        if (id == FD_EVT_WIFI_CONNECTED) populate();
    }
}

static void on_tick(void)
{
    /* cap nhat RSSI khi da ket noi (giu thong bao loi neu dang hien) */
    if (fd_net_state() == FD_NET_CONNECTED && !s_panel) status_from_state();
}

/* Quay lai (vuot mep / giu B1): neu dang mo panel mat khau/tuy chon thi dong panel truoc */
static bool on_back(void)
{
    if (s_fdlg) { fdlg_close(); return true; }   /* hop thoai xac nhan: quay lai = Huy */
    if (!s_panel) return false;
    panel_close();
    status_from_state();
    return true;
}

const ui_screen_t g_scr_wifi = {
    .name = "wifi", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_event = on_event,
    .on_tick = on_tick,
    .on_back = on_back,
};
