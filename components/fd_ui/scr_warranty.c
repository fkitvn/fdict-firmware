/* Settings > Warranty information: fetch on entry/retry only; read-only API. */
#include <stdio.h>
#include "fd_fonts.h"
#include "fd_zlink.h"
#include "ui_priv.h"

#define WARRANTY_MINI_APP_URL "https://zalo.me/s/3845974445044166380/"
static lv_obj_t *s_status, *s_hint, *s_qr_wrap, *s_retry;
static lv_timer_t *s_tm;

static void fill(void)
{
    if (!s_status) return;
    fd_warranty_status_t st;
    fd_zlink_status_get(&st);
    bool guide = st.state == FD_WARR_READY && !st.activated;
    lv_obj_set_hidden(s_hint, !guide);
    lv_obj_set_hidden(s_qr_wrap, !guide);
    lv_obj_set_hidden(s_retry, st.state != FD_WARR_ERROR);
    char b[128];
    uint32_t color = 0x6B7280;
    if (st.state == FD_WARR_ERROR) {
        snprintf(b, sizeof(b), _("WARRANTY_STATUS_ERROR"), st.err);
        color = 0xDC2626;
    } else if (st.state != FD_WARR_READY) {
        snprintf(b, sizeof(b), "%s", _("LOADING"));
    } else if (!st.activated) {
        snprintf(b, sizeof(b), "%s", _("WARRANTY_NEEDED"));
    } else {
        snprintf(b, sizeof(b), _(st.active ? "WARRANTY_UNTIL" : "WARRANTY_EXPIRED_ON"), st.expires);
        color = st.active ? 0x16A34A : 0xDC2626;
    }
    lv_label_set_text(s_status, b);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
}
static void retry_cb(lv_event_t *e) { (void)e; fd_zlink_refresh(); fill(); }
static void tm_cb(lv_timer_t *t) { (void)t; fill(); } /* UI polling only, no requests */

static lv_obj_t *label(lv_obj_t *parent, const char *value)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, value);
    return l;
}
static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(col, 12, 0);
    lv_obj_set_style_pad_ver(col, 4, 0);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, false);
    lv_obj_t *title = label(col, _("WARRANTY_INFO"));
    lv_obj_set_style_text_font(title, &fd_font_noto_18, 0);
    /* Same immutable eFuse ID as the pairing QR; independent of API state. */
    char device_id[16];
    ui_device_ident(device_id, sizeof(device_id));
    lv_obj_t *device = label(col, "");
    lv_label_set_text_fmt(device, "%s: %s", _("DEVICE_ID"), device_id);
    s_status = label(col, _("LOADING"));
    s_hint = label(col, _("WARRANTY_ZALO_HINT"));
    s_qr_wrap = lv_obj_create(col);
    lv_obj_remove_style_all(s_qr_wrap);
    lv_obj_set_size(s_qr_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s_qr_wrap, 12, 0); /* >=4 quiet modules at 3 px/module */
    lv_obj_set_style_bg_color(s_qr_wrap, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_qr_wrap, LV_OPA_COVER, 0);
    lv_obj_t *qr = lv_qrcode_create(s_qr_wrap);
    lv_qrcode_set_size(qr, 96);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, WARRANTY_MINI_APP_URL, sizeof(WARRANTY_MINI_APP_URL) - 1);
    s_retry = lv_button_create(col);
    lv_obj_set_size(s_retry, 140, 36);
    lv_obj_t *rt = lv_label_create(s_retry);
    lv_label_set_text(rt, _("RETRY")); lv_obj_center(rt);
    lv_obj_add_event_cb(s_retry, retry_cb, LV_EVENT_CLICKED, NULL);
    fd_zlink_refresh();
    fill();
    s_tm = lv_timer_create(tm_cb, 300, NULL);
}
static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    s_status = s_hint = s_qr_wrap = s_retry = NULL;
}
const ui_screen_t g_scr_warranty = {
    .name = "warranty", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};
