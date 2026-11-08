/* Settings > Zalo link: short-lived, one-use code only. No device ID in URL. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_net.h"
#include "fd_zlink.h"
#include "ui_priv.h"
#define QR_PX 135   /* Same portrait QR and quiet-zone geometry as the login screen. */
static lv_obj_t *s_qr, *s_wrap, *s_code, *s_msg, *s_action, *s_action_lbl, *s_dlg;
static bool s_low;
static lv_timer_t *s_timer;
static char s_drawn[9];
static void render(void)
{
    fd_zalo_link_tick();
    fd_link_status_t st;
    fd_zalo_link_get(&st);
    bool paired = fd_ota_is_paired();
    bool connected = fd_net_is_connected();
    bool linked = st.state == FD_LINK_DONE;
    bool unlinking = st.state == FD_LINK_UNLINKING;
    bool show = st.state == FD_LINK_CODE && st.seconds > 0 && paired && connected;
    lv_obj_set_hidden(s_wrap, !show);
    lv_obj_set_hidden(s_code, !show || s_low);
    if (s_dlg && !linked) { lv_obj_delete(s_dlg); s_dlg = NULL; }
    uint32_t color = 0x6B7280;
    if (show) {
        if (strcmp(st.code, s_drawn)) {
            char url[96];
            snprintf(url, sizeof(url), "https://zalo.me/s/3845974445044166380/?code=%s", st.code);
            lv_qrcode_update(s_qr, url, strlen(url));
            strlcpy(s_drawn, st.code, sizeof(s_drawn));
            lv_label_set_text(s_code, st.code);
        }
        /* Landscape has exactly the login screen's five-row height budget.
         * Keep manual entry available inline instead of adding a sixth row. */
        if (s_low) lv_label_set_text_fmt(s_msg, "%s · %d:%02d", st.code, st.seconds / 60, st.seconds % 60);
        else lv_label_set_text_fmt(s_msg, "%d:%02d", st.seconds / 60, st.seconds % 60);
    } else {
        s_drawn[0] = 0;
        const char *message = !paired ? _("ZALO_LOGIN_REQUIRED") :
            unlinking ? _("ZALO_UNLINKING") :
            linked ? (st.err[0] ? _("ZALO_UNLINK_ERROR") : _("ZALO_LINKED")) :
            !connected ? _("NOT_CONNECTED") :
            st.state == FD_LINK_ERROR ? _("ZALO_CODE_ERROR") : _("LOADING");
        lv_label_set_text(s_msg, message);
        if (linked) color = st.err[0] ? 0xDC2626 : 0x16A34A;
        else if (!paired || !connected || st.state == FD_LINK_ERROR) color = 0xDC2626;
    }
    lv_obj_set_style_text_color(s_msg, lv_color_hex(color), 0);
    lv_label_set_text(s_action_lbl, unlinking ? _("ZALO_UNLINKING") : linked ? _("ZALO_UNLINK") :
        !connected ? _("OPEN_WIFI") : st.state == FD_LINK_ERROR ? _("RETRY") : _("ZALO_NEW_CODE"));
    lv_obj_set_style_bg_color(s_action, lv_color_hex(linked || unlinking ? 0xDC2626 : FD_COLOR_PRIMARY), 0);
    lv_obj_set_state(s_action, LV_STATE_DISABLED, !paired || unlinking || (linked && !connected) ||
        (st.state == FD_LINK_LOADING && connected));
}
static void timer_cb(lv_timer_t *t) { (void)t; render(); }
static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return l;
}
static void dialog_close(bool confirm)
{
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }
    if (confirm) { fd_zalo_link_unlink(); render(); }
}
static void dialog_btn_cb(lv_event_t *e) { dialog_close((bool)(intptr_t)lv_event_get_user_data(e)); }
static void dialog_btn(lv_obj_t *row, const char *text, bool confirm)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, lv_color_hex(confirm ? 0xDC2626 : 0xE5E7EB), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, confirm ? lv_color_white() : lv_color_black(), 0);
    lv_label_set_text(l, text); lv_obj_center(l);
    lv_obj_add_event_cb(b, dialog_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)confirm);
}
static void unlink_cb(lv_event_t *e)
{
    (void)e;
    if (s_dlg) return;
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dlg);
    lv_obj_set_size(s_dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_40, 0);
    lv_obj_set_clickable(s_dlg, true);
    lv_obj_t *box = lv_obj_create(s_dlg);
    lv_obj_set_size(box, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_text_font(box, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(box, lv_color_black(), 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(box);
    label(box, _("ZALO_UNLINK_CONFIRM"));
    label(box, _("ZALO_UNLINK_HINT"));
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    dialog_btn(row, _("CANCEL"), false);
    dialog_btn(row, _("ZALO_UNLINK"), true);
}
static bool on_back(void)
{
    if (!s_dlg) return false;
    dialog_close(false);
    return true;
}
static void action_cb(lv_event_t *e)
{
    if (lv_obj_has_state(s_action, LV_STATE_DISABLED)) return;
    fd_link_status_t st;
    fd_zalo_link_get(&st);
    if (st.state == FD_LINK_DONE) { unlink_cb(e); return; }
    if (!fd_net_is_connected()) { fd_ui_show(FD_SCR_WIFI); return; }
    lv_obj_add_state(s_action, LV_STATE_DISABLED);
    fd_zalo_link_start();
    render();
}
static void back_cb(lv_event_t *e) { (void)e; ui_nav_back(); }

/* Identical action-button geometry and colours to Settings > Login. */
static lv_obj_t *button(lv_obj_t *parent, uint32_t bg, lv_obj_t **text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(100), 44);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    *text = lv_label_create(b);
    lv_obj_set_style_text_color(*text, lv_color_white(), 0);
    lv_obj_set_width(*text, lv_pct(100));
    lv_label_set_long_mode(*text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(*text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(*text);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}
static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0x111827), 0);
    s_low = lv_display_get_vertical_resolution(lv_obj_get_display(scr)) < 300;
    lv_obj_set_style_pad_hor(col, 12, 0);
    lv_obj_set_style_pad_ver(col, s_low ? 4 : 6, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, false);
    lv_obj_t *title = label(col, _("ZALO_LINK_TITLE"));
    lv_obj_set_style_text_font(title, &fd_font_noto_14, 0);
    s_wrap = lv_obj_create(col);
    lv_obj_remove_style_all(s_wrap);
    lv_obj_set_size(s_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_wrap, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_wrap, 12, 0);
    lv_obj_set_style_border_width(s_wrap, 1, 0);
    lv_obj_set_style_border_color(s_wrap, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_radius(s_wrap, 12, 0);
    lv_obj_set_clickable(s_wrap, false);
    s_qr = lv_qrcode_create(s_wrap);
    lv_qrcode_set_size(s_qr, s_low ? 100 : QR_PX);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    s_code = label(col, "");
    lv_obj_set_style_text_font(s_code, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(s_code, lv_color_hex(0x6B7280), 0);
    s_msg = label(col, _("LOADING"));
    lv_obj_set_style_text_font(s_msg, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(0x6B7280), 0);
    /* Same flexible spacer and equal-width bottom action row as Login. */
    lv_obj_t *sp = lv_obj_create(col);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, 1, 0);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_t *back_label;
    lv_obj_t *back = button(row, 0xE5E7EB, &back_label, back_cb);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0x111827), 0);
    lv_label_set_text(back_label, _("BACK"));
    s_action = button(row, FD_COLOR_PRIMARY, &s_action_lbl, action_cb);
    lv_obj_set_width(back, 0); lv_obj_set_flex_grow(back, 1);
    lv_obj_set_width(s_action, 0); lv_obj_set_flex_grow(s_action, 1);
    s_drawn[0] = 0;
    fd_zalo_link_start();
    render();
    s_timer = lv_timer_create(timer_cb, 300, NULL);
}
static void destroy(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    dialog_close(false);
    fd_zalo_link_stop();
    s_qr = s_wrap = s_code = s_msg = s_action = s_action_lbl = NULL;
}
const ui_screen_t g_scr_zalo_link = {
    .name = "zalo_link", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_back = on_back,
};
