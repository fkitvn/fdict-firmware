/* Che do nha phat trien > Thu 4 nut bam (2.2.8: toi gian).
 * 4 vong tron so 1..4 o day man hinh, dung vi tri nut vat ly (trai -> phai: IO2, IO3, IO14, IO21).
 * Anh xa: board_buttons.c dang ky nut chi so i tren chan s_pins[i] = {BOARD_BTN1..4} = {21, 2, 3, 14}
 * -> vong tim chi so nut theo GPIO (Nut 4 = IO21 = chi so 0, nut "giu de quay lai" toan cuc).
 * Moi lan bam (khong gioi han): vong to day ~250 ms + tieng bip ngan (ui_vol_beep, bo qua neu am thanh dang ban ->
 * khong chong/xep hang). Nut da bam: vien xanh; du 4 -> dau check tren cung.
 * Thoat: nut [<] / vuot mep luon dung; giu Nut 4 chi quay lai SAU KHI du 4 nut (truoc do giu = danh dau Nut 4). */
#include "board_pins.h"
#include "fd_audio.h"
#include "fd_fonts.h"
#include "ui_priv.h"

#define FLASH_MS 250
static const int s_gpio[4] = {BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4};   /* theo chi so nut (driver) */
static const int s_pos_gpio[4] = {2, 3, 14, 21};   /* vong trai -> phai = vi tri nut vat ly */
static lv_obj_t *s_cell[4], *s_num[4], *s_pass;
static lv_timer_t *s_flash_tm[4];
static uint8_t s_done;   /* bit i = da bam nut chi so i */

static int done_count(void)
{
    int n = 0;
    for (int i = 0; i < 4; i++) n += (s_done >> i) & 1;
    return n;
}

static void style_cell(int i, bool lit)
{
    if (!s_cell[i]) return;
    bool done = (s_done >> i) & 1;
    lv_obj_set_style_bg_opa(s_cell[i], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_cell[i], lv_color_hex(lit ? FD_COLOR_PRIMARY : done ? 0x16A34A : 0x9CA3AF), 0);
    lv_obj_set_style_text_color(s_num[i], lv_color_hex(lit ? FD_COLOR_ON_PRIMARY : FD_COLOR_TEXT), 0);
}

static void flash_end_cb(lv_timer_t *t)
{
    int i = (int)(intptr_t)lv_timer_get_user_data(t);
    s_flash_tm[i] = NULL;   /* timer 1 lan: LVGL tu xoa */
    style_cell(i, false);
}

static bool on_button(const fd_evt_button_t *b)
{
    if (!b || b->index < 0 || b->index >= 4) return true;
    if (done_count() == 4 && b->index == 0 && b->action == FD_BTN_LONG_PRESS) return false;   /* da du: giu Nut 4 = quay lai */
    int i = b->index;
    s_done |= 1u << i;
    style_cell(i, true);
    if (s_flash_tm[i]) lv_timer_reset(s_flash_tm[i]);
    else {
        s_flash_tm[i] = lv_timer_create(flash_end_cb, FLASH_MS, (void *)(intptr_t)i);
        lv_timer_set_repeat_count(s_flash_tm[i], 1);
    }
    if (!fd_audio_busy()) ui_vol_beep();   /* bip moi lan bam; dang phat thi bo qua (khong chong) */
    if (s_pass) lv_obj_set_hidden(s_pass, done_count() != 4);
    return true;   /* nuot moi nut khi dang thu */
}

static void exit_cb(lv_event_t *e) { (void)e; ui_nav_back(); }

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *top = lv_obj_create(body);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_t *bk = lv_button_create(top);   /* quay lai */
    lv_obj_set_size(bk, 44, 36);
    lv_obj_set_style_radius(bk, 10, 0);
    lv_obj_set_style_shadow_width(bk, 0, 0);
    lv_obj_t *bl = lv_label_create(bk);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);
    lv_obj_add_event_cb(bk, exit_cb, LV_EVENT_CLICKED, NULL);
    s_pass = lv_label_create(top);   /* du 4 nut */
    lv_obj_set_style_text_font(s_pass, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_pass, lv_color_hex(0x16A34A), 0);
    lv_label_set_text(s_pass, LV_SYMBOL_OK);
    lv_obj_align(s_pass, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(s_pass, true);

    lv_obj_t *sp = lv_obj_create(body);   /* day hang vong xuong day */
    lv_obj_remove_style_all(sp);
    lv_obj_set_width(sp, 1);
    lv_obj_set_flex_grow(sp, 1);

    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int p = 0; p < 4; p++) {   /* p = vi tri trai -> phai; i = chi so nut co GPIO do */
        int i = 0;
        while (i < 3 && s_gpio[i] != s_pos_gpio[p]) i++;
        lv_obj_t *c = s_cell[i] = lv_obj_create(row);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 52, 52);
        lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(c, 3, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(FD_COLOR_PRIMARY), 0);
        lv_obj_set_clickable(c, false);
        s_num[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_num[i], &fd_font_noto_18, 0);
        lv_label_set_text_fmt(s_num[i], "%d", p + 1);
        lv_obj_center(s_num[i]);
    }
    s_done = 0;
    for (int i = 0; i < 4; i++) style_cell(i, false);
}

static void destroy(void)
{
    for (int i = 0; i < 4; i++) {
        if (s_flash_tm[i]) { lv_timer_delete(s_flash_tm[i]); s_flash_tm[i] = NULL; }
        s_cell[i] = s_num[i] = NULL;
    }
    s_pass = NULL;
}

const ui_screen_t g_scr_btntest = {
    .name = "btntest", .parent = FD_SCR_DEVMODE, .build = build, .destroy = destroy, .on_button = on_button,
};
