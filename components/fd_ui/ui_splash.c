/* Man chao: logo Fdict giua man hinh nen trang trong 1 s, moi lan man hinh bat (boot + thuc tu tat man).
 * Logo 88x73 + dong chu "FDict One" (Noto 18). Lop phu tren lv_layer_sys() nen man dang mo phia duoi giu nguyen va hien lai sau khi het 1 s.
 * Trong luc hien: cham bi ui_nav.c chuyen thanh "nha" (touch_read_wrap), nut bi nuot (on_fd_event). */
#include "esp_log.h"
#include "fd_fonts.h"
#include "ui_priv.h"

LV_IMAGE_DECLARE(img_logo_full);   /* img_logo_full.c (logo la thuong hieu fkit.vn, xem NOTICE) */

#define SPLASH_MS 1000
#define FADE_MS   250

static lv_obj_t *s_ov;
static lv_timer_t *s_t;

bool ui_splash_active(void) { return s_ov != NULL; }

static void done_cb(lv_timer_t *t)
{
    (void)t;
    s_t = NULL;   /* repeat 1: LVGL tu xoa */
    if (s_ov) {
        lv_obj_delete(s_ov);
        s_ov = NULL;
    }
    lv_display_trigger_activity(NULL);   /* tinh thoi gian cho tat man tu luc het man chao */
    ESP_LOGD("fd_ui", "splash: done");
}

void ui_splash_show(void)
{
    if (s_ov) {   /* dang hien: keo dai them 1 s */
        if (s_t) lv_timer_reset(s_t);
        return;
    }
    s_ov = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_ov);
    lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_ov, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_ov, true);   /* chan click xuong lop duoi */
    lv_obj_set_scrollable(s_ov, false);
    /* Nhom logo + chu, can giua theo ca 2 chieu (doc 240x320 / ngang 320x240), toa do nguyen */
    lv_obj_t *grp = lv_obj_create(s_ov);
    lv_obj_remove_style_all(grp);
    lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grp, 14, 0);
    lv_obj_set_clickable(grp, false);
    lv_obj_set_scrollable(grp, false);
    lv_obj_t *img = lv_image_create(grp);
    lv_image_set_src(img, &img_logo_full);   /* 88x73, khong zoom */
    lv_obj_t *lbl = lv_label_create(grp);
    lv_obj_set_style_text_font(lbl, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1E293B), 0);   /* slate dam tren nen trang */
    lv_label_set_text(lbl, "FDict One");
    lv_obj_center(grp);
    lv_obj_fade_in(grp, FADE_MS, 0);   /* opa cua nhom ap dung cho ca logo va chu */
    s_t = lv_timer_create(done_cb, SPLASH_MS, NULL);
    lv_timer_set_repeat_count(s_t, 1);
    ESP_LOGD("fd_ui", "splash: show");
}
