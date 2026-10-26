/* Hook UI cho fd_ota: thiet bi "ranh" (khong thao tac >= 60 s hoac man hinh tat, khong dang phat am thanh) va
 * thong bao ngan truoc khi khoi dong lai de cap nhat. Goi tu task fd_ota (khong phai task LVGL) -> tu lay khoa. */
#include "fd_audio.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_ota.h"
#include "ui_priv.h"

#define OTA_IDLE_MS 60000

static bool idle_cb(void)
{
    if (fd_audio_is_playing()) return false;
    if (fd_display_is_asleep()) return true;
    if (!fd_display_lock(100)) return false;
    uint32_t t = lv_display_get_inactive_time(NULL);
    fd_display_unlock();
    return t >= OTA_IDLE_MS;
}

static void before_reboot_cb(void)
{
    if (fd_display_is_asleep()) return;   /* man hinh tat: khoi dong lai im lang */
    if (!fd_display_lock(500)) return;
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_t *l = lv_label_create(bg);
    lv_obj_set_width(l, lv_pct(90));
    lv_obj_set_style_text_font(l, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, _("OTA_RESTARTING"));
    lv_obj_center(l);
    fd_display_unlock();
}

void ui_ota_init(void) { fd_ota_set_hooks(idle_cb, before_reboot_cb); }
