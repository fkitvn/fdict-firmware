/* 2.2.2 Che do nha phat trien > Thu 4 nut bam. 2.2.3: 4 o xep 1 hang ngang o DAY man hinh, dung vi tri nut vat ly
 * (hang nut duoi man hinh, trai -> phai: IO2, IO3, IO14, IO21), nhan theo GPIO.
 * Anh xa: board_buttons.c dang ky nut chi so i tren chan s_pins[i] = {BOARD_BTN1..4} = {21, 2, 3, 14}
 * -> chi so 0 = IO21 (phai cung, nut "giu de quay lai" toan cuc), 1 = IO2, 2 = IO3, 3 = IO14. O tim chi so theo GPIO.
 * Bam (click / double / giu) nut nao -> o do sang + dau check. Du 4 nut -> "Dat".
 * Thoat: vuot mep / nut [Quay lai] luon dung; GIU IO21 (chi so 0, nhu truoc) chi quay lai SAU KHI da dat. */
#include "board_pins.h"
#include "fd_fonts.h"
#include "ui_priv.h"

static const int s_gpio[4] = {BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4};   /* theo chi so nut (driver) */
static const int s_pos_gpio[4] = {2, 3, 14, 21};   /* o tu trai qua phai = vi tri nut vat ly */
static int s_pos[4];   /* chi so nut -> vi tri 0..3 (Nut 1..4) */
static lv_obj_t *s_cell[4], *s_mark[4], *s_status, *s_hint;
static uint8_t s_done;   /* bit i = da bam Bi */
static uint16_t s_cnt[4];

static int done_count(void)
{
    int n = 0;
    for (int i = 0; i < 4; i++) n += (s_done >> i) & 1;
    return n;
}

static void refresh(void)
{
    if (!s_status) return;
    for (int i = 0; i < 4; i++) {
        bool on = (s_done >> i) & 1;
        lv_obj_set_style_bg_color(s_cell[i], lv_color_hex(on ? 0x22C55E : 0xFFFFFF), 0);
        lv_label_set_text_fmt(s_mark[i], _("BTN_TEST_KEY"), s_pos[i] + 1);
    }
    int n = done_count();
    if (n == 4) {
        lv_label_set_text(s_status, _("BTN_TEST_PASS"));
        lv_obj_set_style_text_color(s_status, lv_color_hex(0x15803D), 0);
        lv_label_set_text(s_hint, _("BTN_TEST_EXIT_HINT"));
    } else {
        lv_label_set_text_fmt(s_status, _("BTN_TEST_PROGRESS"), n);
        lv_obj_set_style_text_color(s_status, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
        lv_label_set_text(s_hint, _("BTN_TEST_HINT"));
    }
}

static bool on_button(const fd_evt_button_t *b)
{
    if (!b || b->index < 0 || b->index >= 4) return true;
    bool passed = done_count() == 4;
    if (passed && b->index == 0 && b->action == FD_BTN_LONG_PRESS) return false;   /* da dat: giu B1 = quay lai */
    s_done |= 1u << b->index;
    if (s_cnt[b->index] < 9999) s_cnt[b->index]++;
    if (!passed && done_count() == 4) ui_vol_beep();
    refresh();
    return true;   /* nuot moi nut khi dang thu */
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    s_done = 0;
    for (int i = 0; i < 4; i++) s_cnt[i] = 0;
    refresh();
}

static void exit_cb(lv_event_t *e) { (void)e; ui_nav_back(); }

static lv_obj_t *act_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_scrollable(body, false);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("BTN_TEST"));

    s_status = lv_label_create(body);
    lv_obj_set_style_text_font(s_status, &fd_font_noto_18, 0);
    lv_obj_set_width(s_status, lv_pct(100)); lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    s_hint = lv_label_create(body);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(s_hint, lv_pct(100)); lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    act_btn(row, _("BTN_TEST_RESET"), reset_cb);
    act_btn(row, _("BACK"), exit_cb);

    lv_obj_t *sp = lv_obj_create(body);   /* day hang o xuong day man hinh */
    lv_obj_remove_style_all(sp);
    lv_obj_set_width(sp, 1);
    lv_obj_set_flex_grow(sp, 1);

    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 6, 0);
    for (int p = 0; p < 4; p++) {   /* p = vi tri trai -> phai; i = chi so nut co GPIO do */
        int i = 0;
        while (i < 3 && s_gpio[i] != s_pos_gpio[p]) i++;
        s_pos[i] = p;
        lv_obj_t *c = s_cell[i] = lv_obj_create(grid);
        lv_obj_set_height(c, 64);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_style_radius(c, 10, 0);
        lv_obj_set_style_border_width(c, 3, 0);
        int k = ui_key_colour(i);
        lv_obj_set_style_border_color(c, lv_color_hex(k >= 0 ? ui_key_rgb(k) : FD_COLOR_BORDER), 0);
        lv_obj_set_style_pad_all(c, 2, 0);
        lv_obj_set_scrollable(c, false);
        lv_obj_set_clickable(c, false);
        s_mark[i] = lv_label_create(c);
        lv_obj_set_style_text_font(s_mark[i], &fd_font_noto_14, 0);
        lv_obj_set_style_text_align(s_mark[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_mark[i]);
    }


    s_done = 0;
    for (int i = 0; i < 4; i++) s_cnt[i] = 0;
    refresh();
}

static void destroy(void)
{
    for (int i = 0; i < 4; i++) s_cell[i] = s_mark[i] = NULL;
    s_status = s_hint = NULL;
}

const ui_screen_t g_scr_btntest = {
    .name = "btntest", .parent = FD_SCR_DEVMODE, .build = build, .destroy = destroy, .on_button = on_button,
};
