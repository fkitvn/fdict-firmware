/* Cai dat > Ngu sau sau: chon 2/3/5/10 phut (luu NVS fd/deep_to, mac dinh 300 s). */
#include "fd_fonts.h"
#include "ui_priv.h"

static const int OPTS[] = {120, 180, 300, 600};
#define N_OPTS ((int)(sizeof(OPTS) / sizeof(OPTS[0])))
static lv_obj_t *s_btn[N_OPTS];

static void mark(void)
{
    int cur = ui_sleep_get_deep_timeout();
    for (int i = 0; i < N_OPTS; i++) {
        if (!s_btn[i]) continue;
        bool on = OPTS[i] == cur;
        lv_obj_set_style_bg_color(s_btn[i], on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
        lv_obj_set_style_border_color(s_btn[i], on ? lv_color_hex(FD_COLOR_PRIMARY_DARK) : lv_color_hex(FD_COLOR_BORDER), 0);
    }
}

static void opt_cb(lv_event_t *e)
{
    ui_sleep_set_deep_timeout(OPTS[(int)(intptr_t)lv_event_get_user_data(e)]);
    mark();
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
    lv_label_set_text(t, _("DEEP_SLEEP_AFTER"));

    lv_obj_t *g = lv_obj_create(body);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(g, 8, 0);
    lv_obj_set_style_pad_column(g, 8, 0);
    for (int i = 0; i < N_OPTS; i++) {
        lv_obj_t *b = lv_button_create(g);
        s_btn[i] = b;
        lv_obj_set_size(b, lv_pct(48), 44);   /* 4 muc: 2 cot, cung kieu nut man Thoi gian cho */
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_fmt(l, _("N_MIN"), OPTS[i] / 60);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, opt_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *h = lv_label_create(body);
    lv_obj_set_style_text_color(h, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(h, _("DEEP_SLEEP_HINT"));
    mark();
}

static void destroy(void)
{
    for (int i = 0; i < N_OPTS; i++) s_btn[i] = NULL;
}

const ui_screen_t g_scr_deepsleep = {
    .name = "deepsleep", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};
