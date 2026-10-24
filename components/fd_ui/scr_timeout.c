/* Cai dat > Thoi gian cho man hinh: chon 15/30/60/120/300 giay hoac "Khong tat" (luu NVS fd/scr_to). */
#include "fd_fonts.h"
#include "ui_priv.h"

static const int OPTS[] = {15, 30, 60, 120, 300, 0};
#define N_OPTS ((int)(sizeof(OPTS) / sizeof(OPTS[0])))
static lv_obj_t *s_btn[N_OPTS];

static void mark(void)
{
    int cur = ui_sleep_get_timeout();
    for (int i = 0; i < N_OPTS; i++) {
        if (!s_btn[i]) continue;
        bool on = OPTS[i] == cur;
        lv_obj_set_style_bg_color(s_btn[i], on ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
        lv_obj_set_style_border_color(s_btn[i], on ? lv_color_hex(FD_COLOR_PRIMARY_DARK) : lv_color_hex(FD_COLOR_BORDER), 0);
    }
}

static void opt_cb(lv_event_t *e)
{
    ui_sleep_set_timeout(OPTS[(int)(intptr_t)lv_event_get_user_data(e)]);
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
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(t, "Thời gian chờ màn hình");

    lv_obj_t *g = lv_obj_create(body);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(g, 8, 0);
    lv_obj_set_style_pad_column(g, 8, 0);
    for (int i = 0; i < N_OPTS; i++) {
        lv_obj_t *b = lv_button_create(g);
        s_btn[i] = b;
        lv_obj_set_size(b, lv_pct(31), 44);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
        if (OPTS[i] == 0) lv_label_set_text(l, "Không tắt");
        else if (OPTS[i] >= 60) lv_label_set_text_fmt(l, "%d phút", OPTS[i] / 60);
        else lv_label_set_text_fmt(l, "%d giây", OPTS[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, opt_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *h = lv_label_create(body);
    lv_obj_set_style_text_color(h, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(h, "Chạm màn hình hoặc bấm nút để bật lại");
    mark();
}

static void destroy(void)
{
    for (int i = 0; i < N_OPTS; i++) s_btn[i] = NULL;
}

const ui_screen_t g_scr_timeout = {
    .name = "timeout", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy,
};