/* Cai dat > Kich hoat bao hanh.
 * Chua kich hoat: ma 6 so + huong dan Mini App Zalo. Da kich hoat: han bao hanh. Khong QR. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_zlink.h"
#include "ui_priv.h"

static lv_obj_t *s_code, *s_status, *s_hint;
static lv_timer_t *s_tm;
static bool s_code_on, s_retried;

static void show_line(lv_obj_t *l, bool on)
{
    if (l) lv_obj_set_hidden(l, !on);
}

static void fill(void)
{
    if (!s_status) return;
    char ex[16];
    int w = fd_zlink_warranty(ex, sizeof(ex));
    if (w == 1) {
        if (s_code_on) { fd_zlink_code_stop(); s_code_on = false; }
        s_retried = false;
        show_line(s_code, false);
        show_line(s_hint, false);
        show_line(s_status, true);
        char b[48];
        snprintf(b, sizeof(b), _("WARRANTY_UNTIL"), ex[0] ? ex : "-");
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x16A34A), 0);
        if (strcmp(lv_label_get_text(s_status), b)) lv_label_set_text(s_status, b);
        return;
    }
    if (w < 0 || !fd_ota_is_paired()) {
        if (s_code_on) { fd_zlink_code_stop(); s_code_on = false; }
        show_line(s_code, false);
        show_line(s_status, true);
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x6B7280), 0);
        const char *st = w < 0 ? "-" : _("WARRANTY_NEEDED");
        if (strcmp(lv_label_get_text(s_status), st)) lv_label_set_text(s_status, st);
        show_line(s_hint, w == 0);
        if (w == 0 && strcmp(lv_label_get_text(s_hint), _("PAIR_HINT"))) lv_label_set_text(s_hint, _("PAIR_HINT"));
        return;
    }
    if (!s_code_on) { s_code_on = true; s_retried = false; fd_zlink_code_ensure(); }
    fd_zlink_code_t c;
    fd_zlink_code_get(&c);
    if ((c.state == FD_ZLC_EXPIRED || c.state == FD_ZLC_ERROR) && !s_retried) {
        s_retried = true;
        fd_zlink_code_start();
        fd_zlink_code_get(&c);
    }
    if (c.state == FD_ZLC_CODE) s_retried = false;
    show_line(s_code, true);
    show_line(s_hint, true);
    show_line(s_status, c.state != FD_ZLC_CODE);
    char code[12] = "--- ---";
    const char *sub = _("ZALO_GETTING_CODE");
    if (c.state == FD_ZLC_CODE && c.code[0]) snprintf(code, sizeof(code), "%.3s %.3s", c.code, c.code + 3);
    else if (c.state == FD_ZLC_EXPIRED) sub = _("ZALO_CODE_EXPIRED");
    else if (c.state == FD_ZLC_ERROR) sub = _("ZALO_CODE_ERROR");
    lv_label_set_text(s_code, code);
    if (strcmp(lv_label_get_text(s_hint), _("WARRANTY_ZALO_HINT"))) lv_label_set_text(s_hint, _("WARRANTY_ZALO_HINT"));
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x6B7280), 0);
    if (strcmp(lv_label_get_text(s_status), sub)) lv_label_set_text(s_status, sub);
}

static void tm_cb(lv_timer_t *t)
{
    (void)t;
    fill();
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *col = ui_page_create(scr);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(col, false);

    lv_obj_t *t = lv_label_create(col);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("PAIR_STEP_ZALO"));

    s_code = lv_label_create(col);
    lv_obj_set_style_text_font(s_code, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(s_code, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_text_align(s_code, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_code, 12, 0);
    lv_label_set_text(s_code, "--- ---");
    lv_obj_set_hidden(s_code, true);

    s_status = lv_label_create(col);
    lv_obj_set_style_text_font(s_status, &fd_font_noto_18, 0);
    lv_obj_set_width(s_status, lv_pct(100));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_status, "-");

    s_hint = lv_label_create(col);
    lv_obj_set_width(s_hint, lv_pct(100));
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x374151), 0);
    lv_obj_set_style_pad_top(s_hint, 8, 0);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_hint, _("WARRANTY_ZALO_HINT"));
    lv_obj_set_hidden(s_hint, true);

    s_code_on = s_retried = false;
    fd_zlink_refresh();
    fill();
    s_tm = lv_timer_create(tm_cb, 400, NULL);
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    if (s_code_on) fd_zlink_code_stop();
    s_code_on = false;
    s_code = s_status = s_hint = NULL;
}

const ui_screen_t g_scr_warranty = {
    .name = "warranty", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};
