/* Cai dat > Ghep thiet bi (bo cuc gon, can giua).
 * Chua ghep: can WiFi truoc (nut Mo Wi-Fi); co WiFi -> tu lay ma (POST /api/devices/pair/start), QR {PAIR_WEB_URL}?id=<mac>&hw&fw&b (khong co ma)
 * + dem nguoc + 1 dong huong dan; poll 5 s trong fd_ota (410 -> tu mo phien moi, QR moi). Da ghep: avatar (chu cai dau) + "Xin chao, <ten>!" + nut Huy ghep doi
 * (hop thoai xac nhan -> xoa NVS fdict/device_token + user_name, ve Chua ghep va tu lay ma moi). KHONG hien/log token. Roi man -> dung ghep. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
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
    /* ---- da ghep: avatar + loi chao + Huy ghep doi ---- */
    const char *nk = done ? (has_name ? name : "") : "\x02";
    if (strcmp(nk, s_shown_name)) {
        strlcpy(s_shown_name, nk, sizeof(s_shown_name));
        if (s_av) { lv_obj_delete(s_av); s_av = NULL; }
        if (done) {   /* avatar_url chua tai anh: dung chu cai dau ten (khong ten -> "?") */
            s_av = ui_avatar_initials(s_av_box, has_name ? name : "", 72, 0x2563EB);
            if (has_name) lv_label_set_text_fmt(s_hello, "Xin chào, %s!", name);
            else lv_label_set_text(s_hello, "Xin chào!");
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
    const char *help = "", *bt = "Lấy mã mới";
    if (s_nowifi) {
        col = 0xDC2626;
        snprintf(b, sizeof(b), "Chưa kết nối Wi-Fi");
        help = "Kết nối Wi-Fi để ghép";
        bt = "Mở Wi-Fi";
    } else switch (p.state) {
    case FD_PAIR_REQUESTING: snprintf(b, sizeof(b), "Đang lấy mã…"); break;
    case FD_PAIR_CODE:
        snprintf(b, sizeof(b), "Quét bằng điện thoại · %d:%02d", p.expires_s / 60, p.expires_s % 60);   /* 1 dong: huong dan + dem nguoc */
        break;
    case FD_PAIR_ERROR:
        col = 0xDC2626;
        snprintf(b, sizeof(b), "Lỗi: %s", !strcmp(p.err, "no_wifi") ? "chưa có Wi-Fi" : p.err);
        bt = "Thử lại";
        break;
    default:
        snprintf(b, sizeof(b), "Chưa ghép");
        bt = "Lấy mã";
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
    lv_label_set_text(t, "Huỷ ghép đôi?");
    lv_obj_t *m = lv_label_create(box);
    lv_obj_set_width(m, lv_pct(100));
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(m, lv_color_hex(0x6B7280), 0);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_label_set_text(m, "Máy sẽ ngắt khỏi tài khoản này.");
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    dlg_btn(row, "Không", false);
    dlg_btn(row, "Huỷ ghép", true);
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
    lv_obj_t *t = center_lbl(col, &fd_font_noto_18, 0x111827);
    lv_obj_set_hidden(t, low);
    lv_label_set_text(t, "Ghép thiết bị");
    /* da ghep */
    s_av_box = lv_obj_create(col);   /* giu cho avatar (tao lai khi doi ten) */
    lv_obj_remove_style_all(s_av_box);
    lv_obj_set_size(s_av_box, 72, 72);
    lv_obj_set_style_margin_top(s_av_box, 8, 0);
    lv_obj_set_clickable(s_av_box, false);
    s_hello = center_lbl(col, &fd_font_noto_18, 0x111827);
    s_sub = center_lbl(col, NULL, 0x16A34A);
    lv_label_set_text(s_sub, "Đã ghép với tài khoản");
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
    lv_label_set_text(bl, "Trở lại");
    s_btn = btn(row, FD_COLOR_PRIMARY, &s_btn_lbl, pair_cb);
    s_un_btn = btn(row, 0xDC2626, &s_un_lbl, unpair_cb);
    lv_label_set_text(s_un_lbl, "Huỷ ghép đôi");
    lv_obj_t *bs[3] = {bb, s_btn, s_un_btn};
    for (int i = 0; i < 3; i++) { lv_obj_set_width(bs[i], 0); lv_obj_set_flex_grow(bs[i], 1); }
    s_shown_name[0] = 1; s_shown_name[1] = 0;
    s_dlg = NULL;
    s_want = !fd_ota_is_paired();   /* chua ghep: lay ma ngay khi co WiFi (trong show) */
    s_nowifi = s_stop_sent = false;
    s_srv_seen = fd_ota_server_gen();
    show();
    s_tm = lv_timer_create(tm_cb, 300, NULL);
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }   /* roi man khi hop thoai mo = Khong */
    s_qr = s_qr_wrap = s_av_box = s_av = s_hello = s_sub = s_msg = s_help = s_btn = s_btn_lbl = s_un_btn = s_un_lbl = NULL;
    fd_ota_pair_stop();   /* roi man: dung poll ghep */
}

const ui_screen_t g_scr_pair = {
    .name = "pair", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_back = on_back,
};
