/* Settings > Zalo link: short-lived, one-use code only. No device ID in URL. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_net.h"
#include "fd_zlink.h"
#include "ui_priv.h"
static lv_obj_t *s_qr, *s_wrap, *s_code, *s_msg, *s_retry;
static lv_timer_t *s_timer;
static char s_drawn[9];
static void render(void)
{
    fd_zalo_link_tick();
    fd_link_status_t st;
    fd_zalo_link_get(&st);
    bool show = st.state == FD_LINK_CODE && st.seconds > 0 && fd_ota_is_paired();
    lv_obj_set_hidden(s_wrap, !show);
    lv_obj_set_hidden(s_code, !show);
    lv_obj_set_hidden(s_retry, st.state != FD_LINK_ERROR && st.state != FD_LINK_DONE);
    if (show) {
        if (strcmp(st.code, s_drawn)) {
            char url[96];
            snprintf(url, sizeof(url), "https://zalo.me/s/3845974445044166380/?code=%s", st.code);
            lv_qrcode_update(s_qr, url, strlen(url));
            strlcpy(s_drawn, st.code, sizeof(s_drawn));
            lv_label_set_text(s_code, st.code);
        }
        lv_label_set_text_fmt(s_msg, _("ZALO_CODE_EXPIRES"), st.seconds / 60, st.seconds % 60);
    } else {
        s_drawn[0] = 0;
        lv_label_set_text(s_msg, !fd_ota_is_paired() ? _("ZALO_LOGIN_REQUIRED") :
            st.state == FD_LINK_DONE ? _("ZALO_LINKED") :
            !fd_net_is_connected() ? _("NOT_CONNECTED") :
            st.state == FD_LINK_ERROR ? _("ZALO_CODE_ERROR") : _("LOADING"));
    }
}
static void timer_cb(lv_timer_t *t) { (void)t; render(); }
static void retry_cb(lv_event_t *e) { (void)e; fd_zalo_link_start(); render(); }
static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return l;
}
static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_pad_all(col, 4, 0);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, true);
    label(col, _("ZALO_LINK_TITLE"));
    label(col, _("ZALO_CODE_HINT"));
    s_wrap = lv_obj_create(col);
    lv_obj_remove_style_all(s_wrap);
    lv_obj_set_size(s_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_wrap, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_wrap, 12, 0);
    s_qr = lv_qrcode_create(s_wrap);
    lv_qrcode_set_size(s_qr, 111);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    s_code = label(col, "");
    lv_obj_set_style_text_font(s_code, &fd_font_noto_18, 0);
    s_msg = label(col, _("LOADING"));
    s_retry = lv_button_create(col);
    lv_obj_set_size(s_retry, 150, 34);
    lv_obj_t *l = lv_label_create(s_retry);
    lv_label_set_text(l, _("ZALO_NEW_CODE")); lv_obj_center(l);
    lv_obj_add_event_cb(s_retry, retry_cb, LV_EVENT_CLICKED, NULL);
    s_drawn[0] = 0;
    fd_zalo_link_start();
    render();
    s_timer = lv_timer_create(timer_cb, 300, NULL);
}
static void destroy(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    fd_zalo_link_stop();
    s_qr = s_wrap = s_code = s_msg = s_retry = NULL;
}
const ui_screen_t g_scr_zalo_link = {
    .name = "zalo_link", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};
