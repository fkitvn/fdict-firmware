/* Cai dat > Huong xoay man hinh: goc hien tai (lon) + 4 nut 0/90/180/270 do (90/270 = doc 240x320). Truoc: nut "Xoay 180°".
 * Chi xoay khi nguoi dung cham nut: doi 0 <-> 180 do moi lan (luu NVS, dung lai man). Mo man khong xoay.
 * Bo cuc flex canh giua -> dung cho ca ngang va doc. */
#include "fd_display.h"
#include "fd_fonts.h"
#include "ui_priv.h"

static void go_home_async(void *arg)
{
    (void)arg;
    ui_nav_home();
}

static void rot_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx != fd_display_get_rotation()) ui_nav_set_rotation(idx);   /* 0/1/2/3 = 0/90/180/270 do (luu NVS, dung lai man) */
    /* Ve Home sau khi xoay xong (ke ca bam goc hien tai): lv_async_call chay o vong lv_timer_handler ke tiep,
     * sau khi man da dung lai o do phan giai moi va nut nay (doi tuong cu) da bi xoa. */
    lv_async_call(go_home_async, NULL);
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 10, 0);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);   /* ngang: chu khong tran khung */
    lv_label_set_text(t, "Hướng xoay màn hình");

    /* Montserrat 32 khong co glyph do -> so (Montserrat 32) + "°" (Noto 18) */
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *deg = lv_label_create(row);
    lv_obj_set_style_text_font(deg, &fd_font_mont_32, 0);
    lv_obj_set_style_text_color(deg, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_text_fmt(deg, "%d", fd_display_rotation_deg(fd_display_get_rotation()));
    lv_obj_t *o = lv_label_create(row);
    lv_obj_set_style_text_font(o, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_text(o, "°");

    /* 4 huong: luoi 2x2, nut huong hien tai to dam */
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(grid, 8, 0);
    static const int ORDER[4] = {0, 1, 2, 3};
    for (int n = 0; n < 4; n++) {
        int idx = ORDER[n];
        bool cur = idx == fd_display_get_rotation();
        lv_obj_t *b = lv_button_create(grid);
        lv_obj_set_size(b, lv_pct(45), 48);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        if (!cur) {
            lv_obj_set_style_bg_color(b, lv_color_white(), 0);
            lv_obj_set_style_text_color(b, lv_color_hex(0x202020), 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
        }
        lv_obj_t *bl = lv_label_create(b);
        lv_obj_set_style_text_font(bl, &fd_font_noto_18, 0);
        lv_label_set_text_fmt(bl, "%d°", fd_display_rotation_deg(idx));
        lv_obj_center(bl);
        lv_obj_add_event_cb(b, rot_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    }}

const ui_screen_t g_scr_rotation = {.name = "rotation", .parent = FD_SCR_SETTINGS, .build = build};