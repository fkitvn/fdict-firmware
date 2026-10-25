/* Bang cai dat nhanh (vuot tu mep tren xuong, moi man ke ca Home), tren lv_layer_top.
 * Do sang 10..100 (ap dung ngay, luu khi nha) | Am luong 0..100 (luu + chuong xem truoc khi nha) |
 * 2 hang gon (quick_stats.c): Muc tieu hom nay 12/20 + thanh | Streak 4 ngay + 7 cham T2..CN (duoi tieu de, tren thanh truot).
 * Hang nut: Wi-Fi, Xoay (man chon huong), Home, Cai dat, Ngu. Dong: vuot len tren bang, cham nen mo, quay lai / giu B1, chuyen man. */
#include <stdio.h>
#include <string.h>
#include "fd_audio.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "sdkconfig.h"
#include "fd_pin.h"
#include "ui_priv.h"

#include "ui_swipe.h"
#define QUICK_PANEL_OPA LV_OPA_COVER   /* do dac nen bang nhanh (100% = dac), chinh tai day */
LV_IMAGE_DECLARE(img_rotate);
LV_IMAGE_DECLARE(img_logo);
LV_IMAGE_DECLARE(img_sun);
LV_IMAGE_DECLARE(img_sleep);

static lv_obj_t *s_ov, *s_panel, *s_bri_val, *s_vol_icon, *s_vol_val, *s_vol_sl;

bool ui_quick_is_open(void) { return s_ov != NULL; }

/* dong ho HH:MM canh pin (gio dia phuong CONFIG_FD_TZ; chua dong bo gio -> "--:--"), cap nhat moi phut khi bang mo */
static void clock_update(lv_obj_t *l)
{
    char b[12];
    ui_fmt_clock(b, sizeof(b), false);
    if (strcmp(lv_label_get_text(l), b)) lv_label_set_text(l, b);   /* chi ve lai khi doi phut */
}
static void clock_tmr_cb(lv_timer_t *t) { clock_update((lv_obj_t *)lv_timer_get_user_data(t)); }
static void clock_del_cb(lv_event_t *e) { lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e)); }

bool ui_quick_hit(const lv_point_t *p)
{
    if (!s_panel) return false;
    lv_area_t a;
    lv_obj_get_coords(s_panel, &a);
    /* dung sai FD_SWIPE_TOL_PX quanh bang (vuot len de dong) */
    return p->x >= a.x1 - FD_SWIPE_TOL_PX && p->x <= a.x2 + FD_SWIPE_TOL_PX && p->y >= a.y1 - FD_SWIPE_TOL_PX &&
           p->y <= a.y2 + FD_SWIPE_TOL_PX;
}

static void y_cb(void *o, int32_t v) { lv_obj_set_y(o, v); }
static void ov_opa_cb(void *o, int32_t v) { lv_obj_set_style_bg_opa(o, (lv_opa_t)v, 0); }
static void gone_cb(lv_anim_t *a) { lv_obj_delete(a->var); }

void ui_quick_close(bool anim)
{
    if (!s_ov) return;
    lv_obj_t *ov = s_ov, *pn = s_panel;
    s_ov = s_panel = s_bri_val = s_vol_icon = s_vol_val = s_vol_sl = NULL;
    lv_anim_delete(ov, NULL);
    lv_anim_delete(pn, NULL);
    if (!anim) {
        lv_obj_delete(ov);
        return;
    }
    lv_obj_set_clickable(ov, false);
    lv_obj_set_clickable(pn, false);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pn);
    lv_anim_set_exec_cb(&a, y_cb);
    lv_anim_set_values(&a, lv_obj_get_y(pn), -lv_obj_get_height(pn));
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);
    lv_anim_init(&a);
    lv_anim_set_var(&a, ov);
    lv_anim_set_exec_cb(&a, ov_opa_cb);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_completed_cb(&a, gone_cb);   /* xoa ca panel (con cua nen) */
    lv_anim_start(&a);
}

static void ov_click_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_ov) ui_quick_close(true);
}

static void vol_show(int v)
{
    lv_label_set_text(s_vol_icon, v == 0 ? LV_SYMBOL_MUTE : v < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(s_vol_icon, lv_color_hex(v == 0 ? FD_COLOR_TEXT_MUTED : FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_text_fmt(s_vol_val, "%d%%", v);
}

static void bri_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(sl);
    fd_display_set_brightness(v);   /* LEDC ngay (khi dang ngu chi ghi nho) */
    lv_label_set_text_fmt(s_bri_val, "%d%%", fd_display_get_brightness());
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) fd_display_save_brightness();
}

static void vol_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(s_vol_sl);
    vol_show(v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        fd_audio_set_volume(v);
        if (v > 0) ui_vol_beep();   /* 'bip bip' o muc moi */
    }
}

static void wifi_cb(lv_event_t *e)
{
    (void)e;
    ui_quick_close(false);
    fd_ui_show(FD_SCR_WIFI);
}

static void rot_cb(lv_event_t *e)
{
    (void)e;
    ui_quick_close(false);
    if (ui_nav_current() != &g_scr_rotation) fd_ui_show(FD_SCR_ROTATION);   /* man chon 0/90/180/270; quay lai = man dang mo truoc do */
}

static void home_cb(lv_event_t *e)
{
    (void)e;
    ui_quick_close(false);
    if (!ui_pin_wake_locked()) ui_nav_home();   /* async (lv_async_call) nhu scr_rotation go_home_async; dang khoa PIN -> khong di */
}

static void set_cb(lv_event_t *e)
{
    (void)e;
    ui_quick_close(false);
    if (ui_nav_current() != &g_scr_settings) fd_ui_show(FD_SCR_SETTINGS);   /* dang o Cai dat: chi dong */
}

#if CONFIG_FD_SLEEP_BTN
/* ---------- xac nhan Ngu (deep sleep) ---------- */
static lv_obj_t *s_pdlg;

static void pdlg_cb(lv_event_t *e)
{
    bool ok = (bool)(intptr_t)lv_event_get_user_data(e);
    lv_obj_delete(s_pdlg);
    s_pdlg = NULL;
    if (!ok) return;
    fd_display_set_sleep(true);               /* den nen tat NGAY, khong ve gi them */
    if (fd_pin_is_set()) fd_pin_req_set(true); /* thuc lai -> bat nhap PIN truoc Home */
    fd_sleep_enter();                         /* khong tro ve */
}

static void pdlg_btn(lv_obj_t *row, const char *t, bool ok)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ok ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);   /* Ngu = amber chinh */
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, pdlg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ok);
}

static void sleep_cb(lv_event_t *e)
{
    (void)e;
    ui_quick_close(false);
    if (s_pdlg) return;
    s_pdlg = lv_obj_create(lv_layer_top());   /* cung kieu hop thoai devmode */
    lv_obj_remove_style_all(s_pdlg);
    lv_obj_set_size(s_pdlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_pdlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_pdlg, LV_OPA_40, 0);
    lv_obj_set_clickable(s_pdlg, true);
    lv_obj_t *box = lv_obj_create(s_pdlg);
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
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(t, "Ngủ?");
    lv_obj_t *h = lv_label_create(box);
    lv_obj_set_style_text_color(h, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(h, "Bấm nút bất kỳ để bật lại");
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    pdlg_btn(row, "Hủy", false);
    pdlg_btn(row, "Ngủ", true);
}
#endif

static lv_obj_t *mkrow(lv_obj_t *p)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 34);   /* 20/10: 38 -> 34 nhuong cho 2 hang Muc tieu/Streak (ngang 240: bang ~227 px hien) */
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    return r;
}

static lv_obj_t *sym(lv_obj_t *p, const char *s)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_obj_set_width(l, 20);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *slider_row(lv_obj_t *p, const char *icon, const char *name, int lo, int v, lv_event_cb_t cb,
                            lv_obj_t **icon_out, lv_obj_t **val_out)
{
    lv_obj_t *r = mkrow(p);
    lv_obj_t *i;
    if (icon) {
        i = sym(r, icon);
    } else {   /* do sang: icon mat troi (Material Symbols light_mode, A8 20x20) */
        i = lv_image_create(r);
        lv_image_set_src(i, &img_sun);
        lv_obj_set_style_image_recolor(i, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
        lv_obj_set_style_image_recolor_opa(i, LV_OPA_COVER, 0);
    }
    if (icon_out) *icon_out = i;
    (void)name;   /* bo nhan chu: chi icon ben trai de thanh truot dai hon */

    lv_obj_t *sl = lv_slider_create(r);
    lv_obj_set_flex_grow(sl, 1);
    lv_obj_set_height(sl, 12);
    lv_obj_set_ext_click_area(sl, 14);
    lv_slider_set_range(sl, lo, 100);
    lv_slider_set_value(sl, v, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *val = lv_label_create(r);
    lv_obj_set_width(val, 38);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(val, "%d%%", v);
    *val_out = val;
    return sl;
}

/* Nut icon chung cua hang nut bang nhanh: bw x 44, nen trang, vien 1 px, bo goc 10, khong bong, nhan = tint. */
static lv_obj_t *qbtn(lv_obj_t *r, int32_t bw, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(r);
    lv_obj_set_size(b, bw, 44);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY_TINT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

/* Icon ky hieu (Montserrat 14 qua sym(), mau FD_COLOR_ON_PRIMARY) can giua nut */
static lv_obj_t *qsym(lv_obj_t *b, const char *s)
{
    lv_obj_t *l = sym(b, s);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_center(l);
    return l;
}

void ui_quick_open(void)
{
    if (s_ov || ui_pin_wake_locked()) return;   /* khoa PIN sau khi thuc: khong mo bang nhanh */
    int32_t h = lv_display_get_vertical_resolution(NULL);
    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ov);
    lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(s_ov, true);
    lv_obj_set_scrollable(s_ov, false);
    lv_obj_add_event_cb(s_ov, ov_click_cb, LV_EVENT_CLICKED, NULL);

    s_panel = lv_obj_create(s_ov);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_width(s_panel, lv_pct(100));
    lv_obj_set_height(s_panel, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(s_panel, h * 60 / 100, 0);
    lv_obj_set_style_bg_opa(s_panel, QUICK_PANEL_OPA, 0);   
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_white(), 0);
    lv_obj_set_style_radius(s_panel, 14, 0);
    lv_obj_set_style_shadow_width(s_panel, 12, 0);
    lv_obj_set_style_shadow_opa(s_panel, LV_OPA_20, 0);
    lv_obj_set_style_pad_hor(s_panel, 12, 0);
    lv_obj_set_style_pad_top(s_panel, 14 + 6, 0);   /* 14px nam ngoai man (y = -14) */
    lv_obj_set_style_pad_bottom(s_panel, 10, 0);
    lv_obj_set_style_pad_row(s_panel, 3, 0);
    lv_obj_set_style_text_font(s_panel, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(s_panel, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(s_panel, true);   /* cham tren bang khong dong */
    lv_obj_set_scrollable(s_panel, false);

    /* tieu de: logo anh img_logo (mark FD) + ten san pham */
    lv_obj_t *hd = lv_obj_create(s_panel);
    lv_obj_remove_style_all(hd);
    lv_obj_set_size(hd, lv_pct(100), 30);
    lv_obj_set_flex_flow(hd, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hd, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hd, 8, 0);
    lv_obj_t *logo = lv_image_create(hd);   /* mark 'FD' 28x28 (ve 28x23 can giua) RGB565A8 tu fdict-logo-full.svg (19/10) */
    lv_image_set_src(logo, &img_logo);
    lv_obj_t *pn = lv_label_create(hd);
    lv_obj_set_style_text_font(pn, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(pn, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(pn, "Fdict One");
    lv_obj_t *hsp = lv_obj_create(hd);   /* day pin sang phai */
    lv_obj_remove_style_all(hsp);
    lv_obj_set_size(hsp, 1, 1);
    lv_obj_set_flex_grow(hsp, 1);
    lv_obj_t *clk = lv_label_create(hd);   /* dong ho ngay ben trai pin */
    lv_obj_set_style_text_color(clk, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(clk, "");
    clock_update(clk);
    lv_obj_add_event_cb(clk, clock_del_cb, LV_EVENT_DELETE, lv_timer_create(clock_tmr_cb, 60000, clk));
    ui_batt_label_create_ex(hd, true);   /* luon hien, tu cap nhat 1 s khi bang mo */
    lv_obj_t *hsep = lv_obj_create(s_panel);
    lv_obj_remove_style_all(hsep);
    lv_obj_set_size(hsep, lv_pct(100), 1);
    lv_obj_set_style_bg_opa(hsep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hsep, lv_color_hex(FD_COLOR_BORDER), 0);

    slider_row(s_panel, NULL, "Độ sáng", 10, fd_display_get_brightness(), bri_cb, NULL, &s_bri_val);
    int v = fd_audio_get_volume();
    s_vol_sl = slider_row(s_panel, LV_SYMBOL_VOLUME_MAX, "Âm lượng", 0, v, vol_cb, &s_vol_icon, &s_vol_val);
    vol_show(v);

    lv_obj_t *sep = lv_obj_create(s_panel);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, lv_pct(100), 1);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(FD_COLOR_BORDER), 0);

    lv_obj_t *r = mkrow(s_panel);
    lv_obj_set_height(r, 44);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* Hang nut (20/10 09:32): Wi-Fi, Xoay, Home, Cai dat, Ngu - tat ca qua qbtn() (cung kieu). Ngang 320: 5 x 48 + 4 x 12;
     * doc 240 (noi dung ~214 px): 5 x 38 + 4 x 4. */
    bool narrow = lv_display_get_horizontal_resolution(NULL) < 300;
    int32_t bw = narrow ? 38 : 48;
    lv_obj_set_style_pad_column(r, narrow ? 4 : 12, 0);

    /* WiFi: trang thai qua mau icon (ket noi = amber dam, dang ket noi = amber mo, chua = xam + gach cheo) */
    lv_obj_t *w = qbtn(r, bw, wifi_cb);
    lv_obj_t *wi = qsym(w, LV_SYMBOL_WIFI);
    fd_net_state_t ns = fd_net_state();
    lv_obj_set_style_text_color(wi, lv_color_hex(ns == FD_NET_CONNECTED ? FD_COLOR_PRIMARY_DARK
                                                 : ns == FD_NET_CONNECTING ? FD_COLOR_PRIMARY : 0x9CA3AF), 0);
    if (ns == FD_NET_CONNECTING) lv_obj_set_style_text_opa(wi, LV_OPA_60, 0);
    ui_wifi_slash_sync(wi, ns != FD_NET_CONNECTED && ns != FD_NET_CONNECTING);

    qsym(qbtn(r, bw, rot_cb), LV_SYMBOL_REFRESH);    /* mo man Huong xoay */
    qsym(qbtn(r, bw, home_cb), LV_SYMBOL_HOME);      /* ve Home */
    qsym(qbtn(r, bw, set_cb), LV_SYMBOL_SETTINGS);   /* Cai dat */

#if CONFIG_FD_SLEEP_BTN
    lv_obj_t *pb = qbtn(r, bw, sleep_cb);   /* Ngu: icon mat trang (Material Symbols bedtime) */
    lv_obj_t *pi = lv_image_create(pb);
    lv_image_set_src(pi, &img_sleep);
    lv_obj_set_style_image_recolor(pi, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_set_style_image_recolor_opa(pi, LV_OPA_COVER, 0);
    lv_obj_center(pi);
#endif

    /* truot xuong tu mep tren (~200 ms) + nen mo dan */
    lv_obj_update_layout(s_panel);
    int32_t ph = lv_obj_get_height(s_panel);
    lv_obj_set_y(s_panel, -ph);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, y_cb);
    lv_anim_set_values(&a, -ph, -14);   /* bo goc tron phia tren khuat ngoai man */
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ov);
    lv_anim_set_exec_cb(&a, ov_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_30);
    lv_anim_set_duration(&a, 200);
    lv_anim_start(&a);
}
