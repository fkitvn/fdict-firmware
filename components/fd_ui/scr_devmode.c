/* 2.2.2 Cai dat > Che do nha phat trien: cong tac bat/tat (NVS fd/devmode, xac nhan huy ghep + doi server Lab <-> chinh thuc,
 * giu nguyen hanh vi cu cua cong tac trong Cai dat) + cong cu kiem tra: [Thu 4 nut bam] [Thu loa & micro]. */
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_study.h"
#include "ui_priv.h"

static lv_obj_t *s_dev_sw, *s_dlg;
static bool s_dlg_on;   /* trang thai muon chuyen toi */

static void dlg_close(bool confirm)
{
    if (s_dlg) {
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
    if (confirm) {   /* bat/tat: huy ghep + doi server ngay (Lab <-> chinh thuc) */
        ui_devmode_set(s_dlg_on);
        fd_study_cancel();             /* bo ket qua hoc dang tai tu server cu */
        fd_ota_set_server(s_dlg_on);
        fd_ui_toast(s_dlg_on ? _("UNPAIRED_LAB") : _("UNPAIRED_OFFICIAL"));
    }
    else if (s_dev_sw) lv_obj_set_state(s_dev_sw, LV_STATE_CHECKED, !s_dlg_on);   /* Huy -> tra cong tac ve cu */
}

static void dlg_btn_cb(lv_event_t *e) { dlg_close((bool)(intptr_t)lv_event_get_user_data(e)); }

static lv_obj_t *dlg_btn(lv_obj_t *row, const char *txt, bool primary)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 40);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? FD_COLOR_PRIMARY : FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, dlg_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)primary);
    return b;
}

static void dlg_open(void)
{
    s_dlg = lv_obj_create(lv_layer_top());   /* nen mo phu toan man, chan cham xuong duoi */
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
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_style_text_font(box, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(0x111827), 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, s_dlg_on ? _("TURN_ON_DEVELOPER_MODE") : _("TURN_OFF_DEVELOPER_MODE"));
    lv_obj_t *w = lv_label_create(box);
    lv_obj_set_width(w, lv_pct(100));
    lv_label_set_long_mode(w, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(w, lv_color_hex(0x6B7280), 0);
    lv_label_set_text(w, _("DEVMODE_UNPAIR_WARN"));
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    dlg_btn(row, _("CANCEL"), false);
    dlg_btn(row, s_dlg_on ? _("ON") : _("OFF"), true);
}

static void dev_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    if (on == fd_ui_devmode_get() || s_dlg) return;
    s_dlg_on = on;   /* bat va tat deu can xac nhan (huy ghep + doi server) */
    dlg_open();
}

/* Vuot quay lai / giu B1 khi hop thoai dang mo = Huy */
static bool on_back(void)
{
    if (!s_dlg) return false;
    dlg_close(false);
    return true;
}

static void go_cb(lv_event_t *e)
{
    if (s_dlg) return;
    fd_ui_show((fd_scr_t)(intptr_t)lv_event_get_user_data(e));
}

static void row_style(lv_obj_t *r)
{
    lv_obj_set_size(r, lv_pct(100), 48);
    lv_obj_set_style_bg_color(r, lv_color_white(), 0);
    lv_obj_set_style_text_color(r, lv_color_hex(0x202020), 0);
    lv_obj_set_style_shadow_width(r, 0, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 10, 0);
}

static void row_text(lv_obj_t *r, const char *icon, const char *text)
{
    lv_obj_t *i = lv_label_create(r);
    lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);
    lv_label_set_text(i, icon);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, text);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
}

static void tool_item(lv_obj_t *parent, const char *icon, const char *text, fd_scr_t to)
{
    lv_obj_t *b = lv_button_create(parent);
    row_style(b);
    row_text(b, icon, text);
    lv_obj_t *r = lv_label_create(b);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
    lv_label_set_text(r, LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(intptr_t)to);
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_scrollable(body, true);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("DEV_SECTION"));   /* 2.2.4: tieu de; cong tac ben trong van la DEVELOPER_MODE */

    lv_obj_t *r = lv_obj_create(body);   /* cong tac bat/tat */
    row_style(r);
    lv_obj_set_style_pad_hor(r, 16, 0);
    row_text(r, LV_SYMBOL_WARNING, _("DEVELOPER_MODE"));
    s_dev_sw = lv_switch_create(r);
    lv_obj_set_size(s_dev_sw, 46, 24);
    lv_obj_set_state(s_dev_sw, LV_STATE_CHECKED, fd_ui_devmode_get());
    lv_obj_add_event_cb(s_dev_sw, dev_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *h = lv_label_create(body);
    lv_obj_set_style_text_color(h, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_label_set_text(h, _("DEV_TOOLS"));

    tool_item(body, LV_SYMBOL_KEYBOARD, _("BTN_TEST"), FD_SCR_BTNTEST);
    tool_item(body, LV_SYMBOL_AUDIO, _("MIC_TEST"), FD_SCR_MICTEST);   /* 2.2.3: chuyen tu Cai dat */
}

static void destroy(void)
{
    if (s_dlg) {   /* roi man khi hop thoai mo = Huy */
        lv_obj_delete(s_dlg);
        s_dlg = NULL;
    }
    s_dev_sw = NULL;
}

const ui_screen_t g_scr_devmode = {
    .name = "devmode", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_back = on_back,
};
