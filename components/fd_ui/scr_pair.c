/* Cai dat > Ghep thiet bi (bo cuc gon, can giua).
 * Chua ghep: can WiFi truoc (nut Mo Wi-Fi); co WiFi -> tu lay ma (POST /api/devices/pair/start), QR {PAIR_WEB_URL}?id=<mac>&hw&fw&b (khong co ma)
 * + dem nguoc + 1 dong huong dan; poll 5 s trong fd_ota (410 -> tu mo phien moi, QR moi). Da ghep: avatar (chu cai dau) + "Xin chao, <ten>!" + nut Huy ghep doi
 * (hop thoai xac nhan -> xoa NVS fdict/device_token + user_name, ve Chua ghep va tu lay ma moi). KHONG hien/log token. Roi man -> dung ghep.
 * 2.0.0: 2 buoc tren cung man. Thanh buoc [1 Dinh danh] [2 Lien ket Zalo]. Buoc 1 = QR/web nhu tren. Ghep xong -> chuyen thang Buoc 2:
 * ma 6 so (fd_zlink, 5 phut, dem nguoc, Lay ma moi, De sau). Mo lai khi da ghep ma Zalo chua lien ket -> vao thang Buoc 2.
 * Da lien ket -> ca 2 buoc dau tick + thong tin tai khoan / Huy ghep doi nhu cu. Ma lien ket KHONG log. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_zlink.h"
#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "ui_priv.h"

/* Trang ghep tren web (QR ?id=<mac>&hw&fw&b, khong ma/token). LAB: may PC trong LAN; production: https */
#define PAIR_WEB_URL fd_ota_pair_web_url()   /* theo server hien tai (che do nha phat trien), doc lai moi lan ve QR */
#define QR_PX 135   /* URL ~116-121 B -> QR v7 (45 module, ECC M) x 3 px = 135; + vien trang 12 px (4 module) = 159 px */

static lv_obj_t *s_qr, *s_qr_wrap;
static uint32_t s_qr_sess;   /* phien da ve QR (0 = chua ve) */
static lv_obj_t *s_av_box, *s_av, *s_hello, *s_sub, *s_msg, *s_help, *s_btn, *s_btn_lbl, *s_un_btn, *s_un_lbl, *s_dlg;
static lv_timer_t *s_tm;
static bool s_want;        /* can lay ma (chua ghep / bam nut) - cho WiFi neu chua co */
static bool s_nowifi, s_stop_sent;
static char s_shown_name[64] = "\x01";
static uint32_t s_srv_seen;   /* fd_ota_server_gen da xu ly */
/* 2 buoc */
static lv_obj_t *s_steps, *s_st_ico[2], *s_st_txt[2], *s_zl_code, *s_zl_hint, *s_back_lbl;
static bool s_step2, s_zl_started, s_seen_unpaired, s_low;

/* Ma thiet bi co dinh = MAC goc eFuse (12 hex thuong), vd 84c7bb78c178. Khong doi khi build/OTA/xoa NVS. */
void ui_device_ident(char *out, size_t n)
{
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(out, n, "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* them "&k=<v da ma hoa %>" (giu ky tu unreserved RFC 3986) */
static void q_add(char *u, size_t n, const char *k, const char *v)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t i = strlen(u);
    i += (size_t)snprintf(u + i, i < n ? n - i : 0, "&%s=", k);
    for (const unsigned char *p = (const unsigned char *)v; *p && i + 4 < n; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '.' || *p == '_' || *p == '~') {
            u[i++] = (char)*p;
        } else {
            u[i++] = '%'; u[i++] = HEX[*p >> 4]; u[i++] = HEX[*p & 15];
        }
    }
    if (i < n) u[i] = 0; else u[n - 1] = 0;
}

static void build_qr_url(char *u, size_t n)
{
    char id[16], b[11];
    ui_device_ident(id, sizeof(id));
    esp_app_get_elf_sha256(b, sizeof(b));   /* build_id = 10 hex dau SHA256 ELF */
    snprintf(u, n, "%s?id=%s", PAIR_WEB_URL, id);   /* KHONG BAO GIO dua token/ma vao QR */
    q_add(u, n, "hw", CONFIG_FD_OTA_HW);
    q_add(u, n, "fw", esp_app_get_description()->version);
    q_add(u, n, "b", b);
}

/* 1 ky tu UTF-8 (chu cai dau) cua tu bat dau tai p -> out */
static void first_cp(const char *p, char *out)
{
    int n = 1;
    unsigned char c = (unsigned char)*p;
    if (c >= 0xF0) n = 4; else if (c >= 0xE0) n = 3; else if (c >= 0xC0) n = 2;
    memcpy(out, p, n);
    out[n] = 0;
    if (n == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

lv_obj_t *ui_avatar_initials(lv_obj_t *parent, const char *name, int px, uint32_t bg)
{
    lv_obj_t *a = lv_obj_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, px, px);
    lv_obj_set_style_radius(a, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(a, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(a, 2, 0);
    lv_obj_set_style_border_color(a, lv_color_white(), 0);
    lv_obj_set_clickable(a, false);
    lv_obj_set_event_bubble(a, true);
    char ini[10] = "";
    const char *s = name ? name : "";
    while (*s == ' ') s++;
    if (*s) {
        first_cp(s, ini);
        const char *last = strrchr(s, ' ');
        while (last && last[1] == 0) last = NULL;   /* bo khoang trang cuoi */
        if (last && last[1]) first_cp(last + 1, ini + strlen(ini));
    }
    lv_obj_t *l = lv_label_create(a);
    lv_obj_set_style_text_font(l, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, ini[0] ? ini : "?");
    lv_obj_center(l);
    return a;
}

static void set_txt(lv_obj_t *l, const char *t) { if (strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }

/* trang thai 1 buoc: 0 chua toi (xam), 1 dang lam (mau chinh), 2 xong (xanh + dau tick) */
static void step_set(int i, int st)
{
    static const char *NUM[2] = {"1", "2"};
    uint32_t c = st == 2 ? 0x16A34A : st == 1 ? FD_COLOR_PRIMARY : 0x9CA3AF;
    set_txt(s_st_ico[i], st == 2 ? LV_SYMBOL_OK : NUM[i]);
    lv_obj_set_style_bg_color(s_st_ico[i], lv_color_hex(c), 0);
    lv_obj_set_style_text_color(s_st_txt[i], lv_color_hex(st ? c : 0x6B7280), 0);
}

static void zl_stop(void)
{
    if (s_zl_started) { fd_zlink_code_stop(); s_zl_started = false; }
}

/* Buoc 2: tra ve true neu dang hien Buoc 2 (da ve xong) */
static bool show_step2(void)
{
    if (!s_step2) return false;
    if (!s_zl_started) { s_zl_started = true; fd_zlink_code_ensure(); }
    fd_zlink_code_t c;
    fd_zlink_code_get(&c);
    if (c.state == FD_ZLC_DONE) {   /* lien ket xong -> man tai khoan, 2 buoc tick */
        s_step2 = false;
        zl_stop();
        return false;
    }
    step_set(0, 2);
    step_set(1, 1);
    lv_obj_set_hidden(s_steps, false);
    lv_obj_t *hide[] = {s_av_box, s_hello, s_sub, s_un_btn, s_qr_wrap, s_help};
    for (size_t i = 0; i < sizeof(hide) / sizeof(hide[0]); i++) lv_obj_set_hidden(hide[i], true);
    s_qr_sess = 0;
    lv_obj_set_hidden(s_zl_code, false);
    lv_obj_set_hidden(s_zl_hint, false);
    lv_obj_set_hidden(s_msg, false);
    lv_obj_set_hidden(s_btn, false);
    set_txt(s_back_lbl, _("LATER"));
    char b[64], code[10];
    uint32_t col = 0x6B7280;
    const char *bt = _("ZALO_NEW_CODE");
    bool wifi = fd_net_is_connected();
    switch (c.state) {
    case FD_ZLC_CODE:
        snprintf(code, sizeof(code), "%.3s %.3s", c.code, c.code + 3);
        snprintf(b, sizeof(b), _("ZALO_CODE_EXPIRES"), c.expires_s / 60, c.expires_s % 60);
        break;
    case FD_ZLC_EXPIRED:
        strcpy(code, "--- ---");
        col = 0xDC2626;
        snprintf(b, sizeof(b), "%s", _("ZALO_CODE_EXPIRED"));
        break;
    case FD_ZLC_ERROR:
        strcpy(code, "--- ---");
        col = 0xDC2626;
        if (!wifi || !strcmp(c.err, "no_wifi")) { snprintf(b, sizeof(b), "%s", _("WIFI_NOT_CONNECTED")); bt = _("OPEN_WIFI"); }
        else snprintf(b, sizeof(b), "%s (%s)", _("ZALO_CODE_ERROR"), c.err);
        break;
    default:
        strcpy(code, "--- ---");
        snprintf(b, sizeof(b), "%s", _("ZALO_GETTING_CODE"));
        break;
    }
    set_txt(s_zl_code, code);
    set_txt(s_msg, b);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(col), 0);
    set_txt(s_btn_lbl, bt);
    lv_obj_set_state(s_btn, LV_STATE_DISABLED, c.state == FD_ZLC_REQUESTING);
    return true;
}

static void show(void)
{
    if (!s_msg) return;
    if (s_srv_seen != fd_ota_server_gen()) {   /* doi server: da huy ghep -> lay ma + QR moi tu server moi */
        s_srv_seen = fd_ota_server_gen();
        if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }
        s_want = true;
        s_qr_sess = 0;
        s_shown_name[0] = 1; s_shown_name[1] = 0;
    }
    fd_pair_status_t p;
    fd_ota_pair_get(&p);
    char name[64];
    bool has_name = fd_ota_get_user_name(name, sizeof(name));
    bool busy = p.state == FD_PAIR_REQUESTING || p.state == FD_PAIR_CODE;
    bool done = p.paired && !busy;
    /* ---- 2 buoc ---- */
    if (!done) { s_seen_unpaired = true; s_step2 = false; zl_stop(); }
    else if (s_seen_unpaired) {   /* Buoc 1 vua xong tren man nay -> chuyen thang Buoc 2 */
        s_seen_unpaired = false;
        s_step2 = true;   /* 2.1.2: vao Buoc 2 ngay, xin ma luon (start tra linked neu da kich hoat), khong hoi status truoc */
    } else if (!s_step2 && !s_zl_started && fd_zlink_linked() == FD_ZL_NO) s_step2 = true;   /* mo lai: tiep tuc Buoc 2 */
    if (done && show_step2()) return;
    lv_obj_set_hidden(s_zl_code, true);
    lv_obj_set_hidden(s_zl_hint, true);
    set_txt(s_back_lbl, _("BACK"));
    step_set(0, done ? 2 : 1);
    step_set(1, done && fd_zlink_linked() == FD_ZL_YES ? 2 : 0);
    lv_obj_set_hidden(s_steps, !done && s_low);   /* ngang: Buoc 1 can ca chieu cao cho QR */
    /* ---- da ghep: avatar + loi chao + Huy ghep doi ---- */
    const char *nk = done ? (has_name ? name : "") : "\x02";
    if (strcmp(nk, s_shown_name)) {
        strlcpy(s_shown_name, nk, sizeof(s_shown_name));
        if (s_av) { lv_obj_delete(s_av); s_av = NULL; }
        if (done) {   /* avatar_url chua tai anh: dung chu cai dau ten (khong ten -> "?") */
            s_av = ui_avatar_initials(s_av_box, has_name ? name : "", 72, 0x2563EB);
            if (has_name) lv_label_set_text_fmt(s_hello, _("HELLO_NAME"), name);
            else lv_label_set_text(s_hello, _("HELLO"));
        }
    }
    lv_obj_set_hidden(s_av_box, !done);
    lv_obj_set_hidden(s_hello, !done);
    lv_obj_set_hidden(s_sub, !done);
    lv_obj_set_hidden(s_un_btn, !done);
    if (done) {
        lv_obj_set_hidden(s_qr_wrap, true);
        lv_obj_set_hidden(s_msg, true);
        lv_obj_set_hidden(s_help, true);
        lv_obj_set_hidden(s_btn, true);
        s_qr_sess = 0;
        return;
    }
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }   /* het ghep (vd 401) khi hop thoai mo */
    lv_obj_set_hidden(s_msg, false);
    lv_obj_set_hidden(s_btn, false);
    /* ---- chua ghep: can WiFi truoc ---- */
    if (!fd_net_is_connected()) {   /* khong goi pair/start khi chua co WiFi; dang ghep -> dung poll */
        if (busy) { if (!s_stop_sent) { fd_ota_pair_stop(); s_stop_sent = true; } }
        s_want = true;
        s_nowifi = true;
    } else {
        s_stop_sent = false;
        s_nowifi = false;
        if (s_want && !busy) { s_want = false; fd_ota_pair_start(); busy = true; p.state = FD_PAIR_REQUESTING; }
    }
    char b[48];
    uint32_t col = 0x6B7280;
    const char *help = "", *bt = _("PAIR_GET_NEW_CODE");
    if (s_nowifi) {
        col = 0xDC2626;
        snprintf(b, sizeof(b), _("WIFI_NOT_CONNECTED"));
        help = _("PAIR_CONNECT_WIFI");
        bt = _("OPEN_WIFI");
    } else switch (p.state) {
    case FD_PAIR_REQUESTING: snprintf(b, sizeof(b), _("PAIR_GETTING_CODE")); break;
    case FD_PAIR_CODE:
        snprintf(b, sizeof(b), _("PAIR_SCAN_WITH_PHONE"), p.expires_s / 60, p.expires_s % 60);   /* 1 dong: huong dan + dem nguoc */
        break;
    case FD_PAIR_ERROR:
        col = 0xDC2626;
        snprintf(b, sizeof(b), _("ERROR_FMT"), !strcmp(p.err, "no_wifi") ? _("NO_WIFI") : p.err);
        bt = _("RETRY");
        break;
    default:
        snprintf(b, sizeof(b), _("NOT_PAIRED"));
        bt = _("PAIR_GET_CODE");
        break;
    }
    bool qr = !s_nowifi && p.state == FD_PAIR_CODE;
    lv_obj_set_hidden(s_qr_wrap, !qr);
    if (qr && s_qr_sess != p.session) {   /* phien moi -> ve lai QR */
        char url[200];
        build_qr_url(url, sizeof(url));
        lv_qrcode_update(s_qr, url, strlen(url));
        s_qr_sess = p.session;
    }
    if (!qr) s_qr_sess = 0;
    set_txt(s_msg, b);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(col), 0);
    set_txt(s_help, help);
    lv_obj_set_hidden(s_help, !help[0]);
    set_txt(s_btn_lbl, bt);
    lv_obj_set_state(s_btn, LV_STATE_DISABLED, p.state == FD_PAIR_REQUESTING && !s_nowifi);
}

static void tm_cb(lv_timer_t *t) { (void)t; show(); }

static void pair_cb(lv_event_t *e)   /* Mo Wi-Fi / Lay ma / Lay ma moi / Thu lai */
{
    (void)e;
    if (s_nowifi || !fd_net_is_connected()) { fd_ui_show(FD_SCR_WIFI); return; }   /* mo man WiFi co san */
    if (s_step2) { s_zl_started = true; fd_zlink_code_start(); show(); return; }   /* Buoc 2: Lay ma moi */
    fd_ota_pair_stop();
    fd_ota_pair_start();
}

/* ---- hop thoai xac nhan Huy ghep doi (lv_layer_top) ---- */
static void dlg_close(bool ok)
{
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }
    if (!ok) return;
    fd_ota_unpair();          /* xoa NVS fdict/device_token + user_name (chi tren may, server chua co API huy) */
    s_want = true;            /* ve Chua ghep -> tu lay ma moi de ghep lai */
    s_shown_name[0] = 1; s_shown_name[1] = 0;
}

static void dlg_btn_cb(lv_event_t *e) { dlg_close((bool)(intptr_t)lv_event_get_user_data(e)); }

static void dlg_btn(lv_obj_t *row, const char *txt, bool danger)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(danger ? 0xDC2626 : 0xE5E7EB), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, danger ? lv_color_white() : lv_color_hex(0x111827), 0);
    lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, dlg_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)danger);
}

static void unpair_cb(lv_event_t *e)
{
    (void)e;
    if (s_dlg) return;
    s_dlg = lv_obj_create(lv_layer_top());   /* nen mo phu toan man, chan cham xuong duoi */
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
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_set_style_pad_row(box, 8, 0);
    lv_obj_set_style_text_font(box, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(0x111827), 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(t, _("UNPAIR_CONFIRM"));
    lv_obj_t *m = lv_label_create(box);
    lv_obj_set_width(m, lv_pct(100));
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(m, lv_color_hex(0x6B7280), 0);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_label_set_text(m, _("UNPAIR_MSG"));
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    dlg_btn(row, _("NO"), false);
    dlg_btn(row, _("UNPAIR"), true);
}

/* Vuot quay lai khi hop thoai dang mo = Khong */
static bool on_back(void)
{
    if (!s_dlg) return false;
    dlg_close(false);
    return true;
}

static lv_obj_t *btn(lv_obj_t *parent, uint32_t bg, lv_obj_t **lbl, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(100), 44);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    *lbl = lv_label_create(b);
    lv_obj_set_style_text_color(*lbl, lv_color_white(), 0);
    lv_obj_set_width(*lbl, lv_pct(100)); lv_label_set_long_mode(*lbl, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(*lbl, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(*lbl, "");
    lv_obj_center(*lbl);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *center_lbl(lv_obj_t *parent, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, "");
    return l;
}

static void back_cb(lv_event_t *e) { (void)e; ui_nav_back(); }   /* = vuot mep quay lai (on_back -> ve Cai dat) */

static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0x111827), 0);
    /* Vua 1 man, khong cuon. Doc 240x320: 6 + tieu de 25 + 4 + QR 159 + 4 + dong 20 + (dem) + nut 44 + 6 = 268 <= 320.
     * Ngang 320x240: an tieu de -> 4 + 159 + 4 + 20 + 4 + 44 + 4 = 239 <= 240. Da ghep: 25+4+84+4+25+4+20+(dem)+44+12 = 222. */
    bool low = lv_display_get_vertical_resolution(lv_obj_get_display(scr)) < 300;
    lv_obj_set_style_pad_hor(col, 12, 0);
    lv_obj_set_style_pad_ver(col, low ? 4 : 6, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, false);
    s_low = low;
    /* thanh 2 buoc (thay tieu de, cao 22): (1) Dinh danh  (2) Lien ket Zalo */
    s_steps = lv_obj_create(col);
    lv_obj_remove_style_all(s_steps);
    lv_obj_set_size(s_steps, lv_pct(100), 22);
    lv_obj_set_flex_flow(s_steps, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_steps, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_steps, 4, 0);
    lv_obj_set_clickable(s_steps, false);
    const char *stx[2] = {_("PAIR_STEP_ID"), _("PAIR_STEP_ZALO")};
    for (int i = 0; i < 2; i++) {
        if (i) {   /* gach noi */
            lv_obj_t *ln = lv_obj_create(s_steps);
            lv_obj_remove_style_all(ln);
            lv_obj_set_size(ln, 12, 2);
            lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(ln, lv_color_hex(0xD1D5DB), 0);
        }
        s_st_ico[i] = lv_label_create(s_steps);
        lv_obj_set_size(s_st_ico[i], 20, 20);
        lv_obj_set_style_radius(s_st_ico[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_st_ico[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(s_st_ico[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_st_ico[i], lv_color_white(), 0);
        lv_obj_set_style_text_align(s_st_ico[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(s_st_ico[i], 2, 0);
        lv_label_set_text(s_st_ico[i], "");
        s_st_txt[i] = lv_label_create(s_steps);
        lv_obj_set_style_text_font(s_st_txt[i], &fd_font_noto_14, 0);
        lv_label_set_text(s_st_txt[i], stx[i]);
    }
    /* da ghep */
    s_av_box = lv_obj_create(col);   /* giu cho avatar (tao lai khi doi ten) */
    lv_obj_remove_style_all(s_av_box);
    lv_obj_set_size(s_av_box, 72, 72);
    lv_obj_set_style_margin_top(s_av_box, 8, 0);
    lv_obj_set_clickable(s_av_box, false);
    s_hello = center_lbl(col, &fd_font_noto_18, 0x111827);
    s_sub = center_lbl(col, NULL, 0x16A34A);
    lv_label_set_text(s_sub, _("PAIRED_WITH_ACCOUNT"));
    /* chua ghep */
    s_qr_wrap = lv_obj_create(col);   /* nen trang + vien = quiet zone */
    lv_obj_remove_style_all(s_qr_wrap);
    lv_obj_set_size(s_qr_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_qr_wrap, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_qr_wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_qr_wrap, 12, 0);
    lv_obj_set_style_border_width(s_qr_wrap, 1, 0);
    lv_obj_set_style_border_color(s_qr_wrap, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_radius(s_qr_wrap, 12, 0);
    lv_obj_set_clickable(s_qr_wrap, false);
    s_qr = lv_qrcode_create(s_qr_wrap);
    lv_qrcode_set_size(s_qr, QR_PX);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_obj_set_hidden(s_qr_wrap, true);
    s_qr_sess = 0;
    s_help = center_lbl(col, NULL, 0x111827);
    /* Buoc 2: huong dan + ma 6 so lon (dem nguoc o s_msg) */
    s_zl_hint = center_lbl(col, NULL, 0x111827);
    lv_obj_set_style_margin_top(s_zl_hint, low ? 4 : 16, 0);
    lv_label_set_text(s_zl_hint, _("ZALO_CODE_HINT"));
    s_zl_code = center_lbl(col, &fd_font_mont_32, FD_COLOR_PRIMARY);
    lv_obj_set_style_margin_ver(s_zl_code, low ? 4 : 12, 0);
    s_msg = center_lbl(col, NULL, 0x6B7280);
    /* Hang nut duoi cung (cao 44, bang nhau): [Tro lai (xam, trai)] [nut chinh / Huy ghep doi (do, phai)] */
    lv_obj_t *sp = lv_obj_create(col);   /* dem co gian: day hang nut xuong day man */
    lv_obj_remove_style_all(sp);
    lv_obj_set_width(sp, 1);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t *bl;
    lv_obj_t *bb = btn(row, 0xE5E7EB, &bl, back_cb);
    lv_obj_set_style_text_color(bl, lv_color_hex(0x111827), 0);
    lv_label_set_text(bl, _("BACK"));
    s_back_lbl = bl;   /* Buoc 2: "De sau" (cung hanh vi Tro lai) */
    s_btn = btn(row, FD_COLOR_PRIMARY, &s_btn_lbl, pair_cb);
    s_un_btn = btn(row, 0xDC2626, &s_un_lbl, unpair_cb);
    lv_label_set_text(s_un_lbl, _("LOGOUT"));
    lv_obj_t *bs[3] = {bb, s_btn, s_un_btn};
    for (int i = 0; i < 3; i++) { lv_obj_set_width(bs[i], 0); lv_obj_set_flex_grow(bs[i], 1); }
    s_shown_name[0] = 1; s_shown_name[1] = 0;
    s_dlg = NULL;
    s_want = !fd_ota_is_paired();   /* chua ghep: lay ma ngay khi co WiFi (trong show) */
    s_nowifi = s_stop_sent = false;
    s_srv_seen = fd_ota_server_gen();
    s_step2 = s_zl_started = false;
    s_seen_unpaired = !fd_ota_is_paired();
    if (!s_seen_unpaired) {   /* 2.1.2: da ghep -> xin ma ngay (1 request) thay vi status roi moi start */
        if (fd_zlink_linked() == FD_ZL_YES) fd_zlink_refresh();
        else { s_step2 = true; }
    }
    show();
    s_tm = lv_timer_create(tm_cb, 300, NULL);
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }   /* roi man khi hop thoai mo = Khong */
    zl_stop();   /* roi man: dung poll ma Zalo, xoa ma */
    s_step2 = false;
    s_steps = s_st_ico[0] = s_st_ico[1] = s_st_txt[0] = s_st_txt[1] = s_zl_code = s_zl_hint = s_back_lbl = NULL;
    s_qr = s_qr_wrap = s_av_box = s_av = s_hello = s_sub = s_msg = s_help = s_btn = s_btn_lbl = s_un_btn = s_un_lbl = NULL;
    fd_ota_pair_stop();   /* roi man: dung poll ghep */
}

const ui_screen_t g_scr_pair = {
    .name = "pair", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_back = on_back,
};
