/* Cai dat (khong header): [the lon Ghep thiet bi] | WiFi | Do sang | Am luong | Thoi gian cho | Huong xoay | Thong tin phien ban | Che do nha phat trien | Khoi phuc cai dat goc. */
#include <stdio.h>
#include "esp_system.h"
#include "fd_audio.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_store.h"
#include "fd_zlink.h"
#include "fd_pin.h"
#include "fd_study.h"
#include "ui_priv.h"

static lv_obj_t *s_wifi_sub;
static lv_obj_t *s_rot_sub;
static lv_obj_t *s_bri_sub;
static lv_obj_t *s_vol_sub;
static lv_obj_t *s_to_sub;
static lv_obj_t *s_pin_sub;
static lv_obj_t *s_dev_sub;   /* 2.2.2: Che do nha phat trien -> man rieng */
static lv_obj_t *s_lang_sub;   /* 1.3.0: Ngon ngu / Language */
static lv_obj_t *s_pair_card, *s_pair_sub;

static void go_cb(lv_event_t *e)
{
    fd_ui_show((fd_scr_t)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *item(lv_obj_t *parent, const char *icon, const char *text, lv_event_cb_t cb, void *ud,
                      lv_obj_t **sub)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(100), 48);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0x202020), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 10, 0);
    lv_obj_t *i = lv_label_create(b);
    lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);
    lv_label_set_text(i, icon);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);   /* ngang: chu dai khong tran nut */
    if (sub) {
        *sub = lv_label_create(b);
        lv_obj_set_style_text_color(*sub, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
        lv_obj_set_style_max_width(*sub, lv_pct(45), 0); lv_label_set_long_mode(*sub, LV_LABEL_LONG_DOT);   /* ngang: sub khong day mui ten ra ngoai */
    }
    lv_obj_t *r = lv_label_create(b);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
    lv_label_set_text(r, LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}


/* Hang "Ghep thiet bi" (dau danh sach, cung kieu cac hang khac): avatar/icon | ten + trang thai | > */
static lv_obj_t *s_pair_ico, *s_pair_title;

static void pair_icon_fill(void)
{
    lv_obj_clean(s_pair_ico);
    bool paired = fd_ota_is_paired();
    /* Dang nhap va bao hanh tach roi: hang nay chi noi trang thai dang nhap. */
    lv_obj_set_style_text_color(s_pair_sub, paired ? lv_palette_darken(LV_PALETTE_GREY, 1) : lv_color_hex(FD_COLOR_ALERT), 0);
    char nm[64];
    if (paired && fd_ota_get_user_name(nm, sizeof(nm))) {
        lv_obj_t *av = ui_avatar_initials(s_pair_ico, nm, 36, 0x2563EB);
        lv_obj_center(av);
        lv_label_set_text(s_pair_title, nm);
        lv_label_set_text(s_pair_sub, _("DEVICE_PAIRED"));
    } else {
        lv_obj_t *i = lv_label_create(s_pair_ico);
        lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);
        lv_label_set_text(i, LV_SYMBOL_SHUFFLE);
        lv_obj_center(i);
        if (!paired) {   /* cham thong bao 8 px (xoa cung lv_obj_clean khi da ghep) */
            lv_obj_t *dot = lv_obj_create(s_pair_ico);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(FD_COLOR_ALERT), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_clickable(dot, false);
            lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -4, 6);
        }
        lv_label_set_text(s_pair_title, _("PAIR_DEVICE"));
        lv_label_set_text(s_pair_sub, paired ? _("DEVICE_PAIRED") : _("NOT_PAIRED"));
    }
}

static lv_timer_t *s_zl_tm;
static uint32_t s_zl_gen;
static void zl_tm_cb(lv_timer_t *t)   /* trang thai Zalo tra ve bat dong bo -> ve lai the ghep */
{
    (void)t;
    if (s_zl_gen != fd_zlink_gen()) { s_zl_gen = fd_zlink_gen(); if (s_pair_card) pair_icon_fill(); }
}

static void pair_card(lv_obj_t *parent)
{
    lv_obj_t *b = s_pair_card = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(100), 48);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0x202020), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 10, 0);
    s_pair_ico = lv_obj_create(b);
    lv_obj_remove_style_all(s_pair_ico);
    lv_obj_set_size(s_pair_ico, 36, 36);
    lv_obj_set_clickable(s_pair_ico, false);
    lv_obj_set_event_bubble(s_pair_ico, true);
    lv_obj_t *c = lv_obj_create(b);
    lv_obj_remove_style_all(c);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 0, 0);
    lv_obj_set_clickable(c, false);
    lv_obj_set_event_bubble(c, true);
    s_pair_title = lv_label_create(c);
    lv_obj_set_width(s_pair_title, lv_pct(100));
    lv_label_set_long_mode(s_pair_title, LV_LABEL_LONG_DOT);
    s_pair_sub = lv_label_create(c);
    lv_obj_set_width(s_pair_sub, lv_pct(100));
    lv_label_set_long_mode(s_pair_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_pair_sub, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(s_pair_sub, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_t *r = lv_label_create(b);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
    lv_label_set_text(r, LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FD_SCR_PAIR);
}

/* Hang co cong tac (icon, chu, trang thai, cb VALUE_CHANGED) */
static lv_obj_t *switch_row(lv_obj_t *parent, const char *icon, const char *text, bool checked, lv_event_cb_t cb)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, lv_pct(100), 48);
    lv_obj_set_style_bg_color(r, lv_color_white(), 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_pad_hor(r, 16, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 10, 0);
    lv_obj_t *i = lv_label_create(r);
    lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);
    lv_label_set_text(i, icon);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, text);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);   /* ngang: chu dai khong tran hang */
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 46, 24);
    lv_obj_set_state(sw, LV_STATE_CHECKED, checked);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

/* Che do den trang: ap dung ngay + luu NVS (khong can xac nhan) */
static void gray_cb(lv_event_t *e)
{
    fd_display_set_grayscale(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED), true);
}

/* ---- Khoi phuc cai dat goc: hop thoai xac nhan, roi co + khoi dong lai ---- */
static lv_obj_t *s_dlg, *s_dlg_msg, *s_dlg_row;
static bool s_resetting;

static void dlg_dismiss(void)
{
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }
    s_dlg_msg = s_dlg_row = NULL;
}

static void dlg_close(bool ok)
{
    if (!ok) { dlg_dismiss(); return; }
    if (s_resetting) return;
    s_resetting = true;
    if (s_dlg_msg) lv_label_set_text(s_dlg_msg, _("FACTORY_RESET_WORKING"));
    if (s_dlg_row) lv_obj_set_hidden(s_dlg_row, true);
    lv_refr_now(NULL);
    if (!fd_factory_arm()) {
        s_resetting = false;
        dlg_dismiss();
        fd_ui_toast(_("FACTORY_RESET_FAIL"));
        return;
    }
    esp_restart();
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
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, dlg_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)danger);
}

static void factory_cb(lv_event_t *e)
{
    (void)e;
    if (s_dlg || s_resetting) return;
    fd_ota_status_t st;
    fd_ota_get_status(&st);
    if (st.state == FD_OTA_ST_DOWNLOADING || st.state == FD_OTA_ST_REBOOT_PENDING) {
        fd_ui_toast(_("FACTORY_RESET_BUSY"));
        return;
    }
    s_dlg = lv_obj_create(lv_layer_top());
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
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("FACTORY_RESET_CONFIRM"));
    s_dlg_msg = lv_label_create(box);
    lv_obj_set_width(s_dlg_msg, lv_pct(100));
    lv_obj_set_style_text_align(s_dlg_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_dlg_msg, lv_color_hex(0x6B7280), 0);
    lv_label_set_long_mode(s_dlg_msg, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_dlg_msg, _("FACTORY_RESET_MSG"));
    s_dlg_row = lv_obj_create(box);
    lv_obj_t *row = s_dlg_row;
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    dlg_btn(row, _("CANCEL"), false);
    dlg_btn(row, _("FACTORY_RESET_DO"), true);
}

static bool on_back(void)
{
    if (s_resetting) return true;
    if (!s_dlg) return false;
    dlg_close(false);
    return true;
}

static void refresh(void)
{
    char ssid[33];
    fd_net_ssid(ssid, sizeof(ssid));
    lv_label_set_text(s_wifi_sub, fd_net_is_connected() ? ssid : _("NOT_CONNECTED"));
    lv_label_set_text_fmt(s_bri_sub, "%d%%", fd_display_get_brightness());
    lv_label_set_text_fmt(s_vol_sub, "%d%%", fd_audio_get_volume());
    int to = ui_sleep_get_timeout();
    if (to == 0) lv_label_set_text(s_to_sub, _("NEVER"));
    else if (to % 60 == 0) lv_label_set_text_fmt(s_to_sub, _("N_MIN"), to / 60);
    else lv_label_set_text_fmt(s_to_sub, _("N_SEC"), to);
    lv_label_set_text(s_pin_sub, fd_pin_is_set() ? _("SET") : _("NOT_SET"));
    lv_label_set_text(s_lang_sub, fd_i18n_lang_name(fd_i18n_get_lang()));
    lv_label_set_text(s_dev_sub, fd_ui_devmode_get() ? _("ON") : _("OFF"));
    if (s_pair_card) pair_icon_fill();
    lv_label_set_text_fmt(s_rot_sub, "%d°", fd_display_rotation_deg(fd_display_get_rotation()));
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_scrollable(body, true);
    /* Tieu de lon can giua (font subset 32 px co dau, chi dung o day) */
    ui_screen_title(body, _("SETTINGS"));
    pair_card(body);   /* muc lon dau danh sach */
    s_zl_gen = fd_zlink_gen();
    s_zl_tm = lv_timer_create(zl_tm_cb, 1000, NULL);
    fd_zlink_refresh();
    item(body, LV_SYMBOL_WIFI, "WiFi", go_cb, (void *)(intptr_t)FD_SCR_WIFI, &s_wifi_sub);
    item(body, LV_SYMBOL_KEYBOARD, _("LANGUAGE"), go_cb, (void *)(intptr_t)FD_SCR_LANG, &s_lang_sub);
    item(body, LV_SYMBOL_EYE_OPEN, _("SCREEN_BRIGHTNESS"), go_cb, (void *)(intptr_t)FD_SCR_BRIGHTNESS, &s_bri_sub);
    item(body, LV_SYMBOL_VOLUME_MAX, _("VOLUME"), go_cb, (void *)(intptr_t)FD_SCR_VOLUME, &s_vol_sub);
    item(body, LV_SYMBOL_POWER, _("SCREEN_TIMEOUT"), go_cb, (void *)(intptr_t)FD_SCR_TIMEOUT, &s_to_sub);
    item(body, LV_SYMBOL_REFRESH, _("SCREEN_ROTATION"), go_cb, (void *)(intptr_t)FD_SCR_ROTATION, &s_rot_sub);
    switch_row(body, LV_SYMBOL_IMAGE, _("GRAYSCALE_MODE"), fd_display_get_grayscale(), gray_cb);
    item(body, LV_SYMBOL_EYE_CLOSE, _("PIN_CODE"), go_cb, (void *)(intptr_t)FD_SCR_PIN, &s_pin_sub);
    item(body, LV_SYMBOL_LIST, _("ABOUT"), go_cb, (void *)(intptr_t)FD_SCR_ABOUT, NULL);
    item(body, LV_SYMBOL_WARNING, _("DEV_SECTION"), go_cb, (void *)(intptr_t)FD_SCR_DEVMODE, &s_dev_sub);   /* 2.2.2: man rieng */
    lv_obj_t *rst = item(body, LV_SYMBOL_TRASH, _("FACTORY_RESET"), factory_cb, NULL, NULL);
    lv_obj_set_style_text_color(rst, lv_color_hex(0xDC2626), 0);
    refresh();
}

static void destroy(void) {
    if (s_zl_tm) { lv_timer_delete(s_zl_tm); s_zl_tm = NULL; }
    if (!s_resetting) dlg_dismiss();
    s_wifi_sub = s_rot_sub = s_bri_sub = s_vol_sub = s_to_sub = s_pin_sub = s_lang_sub = s_dev_sub = s_pair_card = s_pair_sub = s_pair_ico = s_pair_title = NULL;
}
static void on_event(int32_t id, void *data) { (void)id; (void)data; refresh(); }

const ui_screen_t g_scr_settings = {
    .name = "settings", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_event = on_event, .on_back = on_back,
};
