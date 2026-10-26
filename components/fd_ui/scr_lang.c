/* Cai dat > Ngon ngu / Language: Tieng Viet | English (luu NVS fd/lang, chon xong tu ve man truoc). */
#include "fd_fonts.h"
#include "ui_priv.h"

static lv_obj_t *s_btn[FD_LANG_COUNT];

static void mark(void)
{
    fd_lang_t cur = fd_i18n_get_lang();
    for (int i = 0; i < FD_LANG_COUNT; i++) {
        if (!s_btn[i]) continue;
        bool on = (int)cur == i;
        lv_obj_set_style_bg_color(s_btn[i], on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
        lv_obj_set_style_border_color(s_btn[i], on ? lv_color_hex(FD_COLOR_PRIMARY_DARK) : lv_color_hex(FD_COLOR_BORDER), 0);
    }
}

static void opt_cb(lv_event_t *e)
{
    fd_lang_t l = (fd_lang_t)(intptr_t)lv_event_get_user_data(e);
    if (l != fd_i18n_get_lang()) {
        fd_i18n_set_lang(l);
        mark();
    }
    ui_nav_back();   /* chon xong -> ve man truoc (Cai dat), man do dung lai theo ngon ngu moi */
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_hor(body, 34, 0);   /* > vung vuot mep 26px */
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(body, false);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, "Ng\u00f4n ng\u1eef / Language");   /* song ngu co dinh */

    for (int i = 0; i < FD_LANG_COUNT; i++) {
        lv_obj_t *b = lv_button_create(body);
        s_btn[i] = b;
        lv_obj_set_size(b, lv_pct(100), 48);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_18, 0);
        lv_label_set_text(l, fd_i18n_lang_name((fd_lang_t)i));
        lv_obj_center(l);
        lv_obj_add_event_cb(b, opt_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    mark();
}

static void destroy(void)
{
    for (int i = 0; i < FD_LANG_COUNT; i++) s_btn[i] = NULL;
}

const ui_screen_t g_scr_lang = {
    .name = "lang", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};
