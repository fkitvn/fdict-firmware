/* Cai dat > Bao hanh. Thoi han tren may + QR toi https://fkit.vn/chinh-sach-doi-tra. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_zlink.h"
#include "ui_priv.h"

#define WARR_URL "https://fkit.vn/chinh-sach-doi-tra"
#define QR_PX 108

static lv_obj_t *s_status;
static lv_timer_t *s_tm;
static uint32_t s_gen;

static void fill(void)
{
    if (!s_status) return;
    char ex[16], b[48];
    int w = fd_zlink_warranty(ex, sizeof(ex));
    uint32_t col = 0x6B7280;
    if (w == 1) {
        snprintf(b, sizeof(b), _("WARRANTY_UNTIL"), ex[0] ? ex : "-");
        col = 0x16A34A;
    } else if (w == 0) {
        snprintf(b, sizeof(b), "%s", _("WARRANTY_NEEDED"));
    } else {
        snprintf(b, sizeof(b), "%s", "-");
    }
    lv_obj_set_style_text_color(s_status, lv_color_hex(col), 0);
    if (strcmp(lv_label_get_text(s_status), b)) lv_label_set_text(s_status, b);
}

static void tm_cb(lv_timer_t *t)
{
    (void)t;
    if (s_gen == fd_zlink_gen()) return;
    s_gen = fd_zlink_gen();
    fill();
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, false);

    lv_obj_t *t = lv_label_create(col);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("PAIR_STEP_ZALO"));

    s_status = lv_label_create(col);
    lv_obj_set_style_text_font(s_status, &fd_font_noto_18, 0);
    lv_obj_set_width(s_status, lv_pct(100));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_status, "-");

    lv_obj_t *qw = lv_obj_create(col);
    lv_obj_remove_style_all(qw);
    lv_obj_set_size(qw, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(qw, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qw, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(qw, 8, 0);
    lv_obj_set_style_border_width(qw, 1, 0);
    lv_obj_set_style_border_color(qw, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_radius(qw, 8, 0);
    lv_obj_set_clickable(qw, false);
    lv_obj_t *qr = lv_qrcode_create(qw);
    lv_qrcode_set_size(qr, QR_PX);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, WARR_URL, strlen(WARR_URL));

    lv_obj_t *cap = lv_label_create(col);
    lv_obj_set_width(cap, lv_pct(100));
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x6B7280), 0);
    lv_label_set_long_mode(cap, LV_LABEL_LONG_WRAP);
    lv_label_set_text(cap, _("POLICY_QR"));

    s_gen = fd_zlink_gen();
    fd_zlink_refresh();
    fill();
    s_tm = lv_timer_create(tm_cb, 500, NULL);
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    s_status = NULL;
}

const ui_screen_t g_scr_warranty = {
    .name = "warranty", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};
