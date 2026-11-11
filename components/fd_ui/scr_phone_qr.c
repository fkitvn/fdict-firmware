/* Bang nhanh > nut Dien thoai: QR de dien thoai quet va mo trang web FDict.
 * URL = goc may chu hien tai fd_ota_base_url() (scheme + host, khong /api), cung server ma thiet bi dang dung:
 *   che do nha phat trien bat -> CONFIG_FD_OTA_LAB_BASE_URL (Lab), tat -> CONFIG_FD_OTA_BASE_URL (chinh thuc).
 * KHONG dua ma thiet bi / token vao QR. Khong can mang (QR ve tai cho).
 * Man toi gian (nhe): chi co the trang chua QR + 1 dong chu huong dan + nut Quay lai. Khong icon, khong hien URL.
 * Ngang (320x240): the QR ben trai; cot phai: dong chu + nut Quay lai ngay duoi, ca khoi can giua doc.
 * Doc (240x320): the QR, dong chu, nut Quay lai ngay duoi, ca khoi can giua.
 * Bam Quay lai: quay lai + goi dong bo tu (fd_sync_force, bat dong bo; bo qua khi chua dang nhap / khong WiFi).
 * Chua dang nhap (chua co device_token): hien cong ui_pair_gate giong cac mo-dun hoc (Quay lai | Dang nhap).
 * Dong: nut Quay lai, vuot mep / giu B1 (ui_nav_back -> man truoc do). */
#include <string.h>
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "ui_priv.h"

#define QR_PX  125   /* URL ngan (~20-32 B) -> QR v2 (25 module) x 5 px = 125 khop; v3 (29) x 4 px + le can giua */
#define QR_PAD 12    /* vien trang cua the = quiet zone (>= 2.4 module, them le khi v3) */

static lv_obj_t *s_qr;
static char s_drawn[96];

static void render(void)
{
    if (!s_qr) return;
    char url[96];
    strlcpy(url, fd_ota_base_url(), sizeof(url));
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') url[--n] = 0;   /* goc trang web: bo '/' cuoi */
    if (!strcmp(url, s_drawn)) return;   /* chi ve lai khi doi server (bat/tat che do nha phat trien) */
    lv_qrcode_update(s_qr, url, strlen(url));
    strlcpy(s_drawn, url, sizeof(s_drawn));
}

static lv_obj_t *spacer(lv_obj_t *p)
{
    lv_obj_t *sp = lv_obj_create(p);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, 1, 0);
    lv_obj_set_flex_grow(sp, 1);
    return sp;
}

/* Quay lai: ve man truoc ngay + xin dong bo tu (fd_sync_force chi day lenh vao hang doi task dong bo, khong cho).
 * Chua dang nhap / chua co WiFi: bo qua dong bo, chi quay lai. */
static void back_cb(lv_event_t *e)
{
    (void)e;
    if (fd_ota_is_paired() && fd_net_is_connected()) fd_sync_force();
    ui_nav_back();
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *page = ui_page_create(scr);
    s_qr = NULL;
    if (!fd_ota_is_paired()) { ui_pair_gate(page); return; }   /* chua dang nhap: cong Dang nhap chung nhu cac mo-dun hoc */
    lv_obj_set_style_text_color(scr, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_display_t *d = lv_obj_get_display(scr);
    bool land = lv_display_get_horizontal_resolution(d) > lv_display_get_vertical_resolution(d);
    lv_obj_set_style_pad_hor(page, 12, 0);
    lv_obj_set_style_pad_ver(page, 10, 0);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_style_pad_column(page, 16, 0);
    lv_obj_set_flex_flow(page, land ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(page, false);

    if (!land) spacer(page);
    /* The QR: nen trang + pad = quiet zone, bo goc, vien mau nhat */
    lv_obj_t *card = lv_obj_create(page);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, QR_PAD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, false);
    s_qr = lv_qrcode_create(card);
    lv_qrcode_set_size(s_qr, QR_PX);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());

    lv_obj_t *side = lv_obj_create(page);   /* ngang: cot phai; doc: phan duoi the QR */
    lv_obj_remove_style_all(side);
    if (land) { lv_obj_set_size(side, 0, lv_pct(100)); lv_obj_set_flex_grow(side, 1); }
    else { lv_obj_set_size(side, lv_pct(100), LV_SIZE_CONTENT); }
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(side, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(side, 12, 0);
    lv_obj_set_scrollable(side, false);
    if (land) spacer(side);

    lv_obj_t *l = lv_label_create(side);   /* dong chu duy nhat */
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(l, _("PHONE_QR_HINT"));

    lv_obj_t *b = lv_button_create(side);   /* cung kieu nut Quay lai man Dang nhap */
    lv_obj_set_size(b, lv_pct(100), 40);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_t *bl = lv_label_create(b);
    lv_obj_set_style_text_color(bl, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(bl, _("BACK"));
    lv_obj_center(bl);
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    if (land) spacer(side);   /* spacer tren + duoi: khoi chu + nut can giua doc */
    if (!land) spacer(page);

    s_drawn[0] = 0;
    render();
}

static void destroy(void)
{
    s_qr = NULL;
    s_drawn[0] = 0;
}

const ui_screen_t g_scr_phone_qr = {
    .name = "phone_qr", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = render,
};
