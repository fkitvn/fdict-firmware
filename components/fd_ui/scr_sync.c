/* 2.3.0 Man "Dong bo": noi dung da sao chep tren may (tu, cau, bo), lan dong bo cuoi, ket qua cho gui, loi;
 * nut [Dong bo ngay]. Dong bo tu chay nen khi co Wi-Fi (fd_sync). */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "ui_priv.h"
#include "fd_store.h"

static lv_obj_t *s_body, *s_v[5], *s_state, *s_btn;
static bool s_gate;

static void wifi_cb(lv_event_t *e) { (void)e; fd_ui_show(FD_SCR_WIFI); }
static void now_cb(lv_event_t *e) { (void)e; fd_sync_force(); }

static lv_obj_t *kv(lv_obj_t *p, const char *k)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *a = lv_label_create(r);
    lv_obj_set_style_text_color(a, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(a, k);
    lv_obj_t *b = lv_label_create(r);
    lv_obj_set_style_text_color(b, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(b, "");
    return b;
}

static void fmt_err(const char *e, char *out, size_t n)
{
    if (!strcmp(e, "nomem")) strlcpy(out, _("MEMORY_ERROR"), n);
    else if (!strcmp(e, "no_wifi")) strlcpy(out, _("WIFI_NOT_CONNECTED"), n);
    else if (!strcmp(e, "unpaired")) strlcpy(out, _("PAIR_TO_STUDY"), n);
    else snprintf(out, n, _("ERROR_FMT"), e);
}

static void refresh(void)
{
    if (s_gate || !s_body) return;
    fd_sync_status_t st;
    fd_sync_get(&st);
    lv_label_set_text_fmt(s_v[0], "%d", st.words);
    lv_label_set_text_fmt(s_v[1], "%d", st.sentences);
    lv_label_set_text_fmt(s_v[2], "%d", st.collections);
    lv_label_set_text_fmt(s_v[4], "%d", st.pending);
    char t[48];
    if (st.last_sync > 0) {
        struct tm lt;
        localtime_r(&st.last_sync, &lt);
        strftime(t, sizeof(t), "%H:%M", &lt);   /* 4.1.10: chi gio dong bo (24h), bo ngay */
    } else strlcpy(t, _("SYNC_NEVER"), sizeof(t));
    lv_label_set_text(s_v[3], t);
    bool run = st.state == FD_SYNC_CHECKING || st.state == FD_SYNC_PULLING || st.state == FD_SYNC_PUSHING || st.manual;
    char m[96];
    uint32_t col = FD_COLOR_TEXT_MUTED;
    if (fd_store_status() != ESP_OK) { strlcpy(m, _("STORAGE_ERROR"), sizeof(m)); col = 0xDC2626; }
    else if (st.state == FD_SYNC_PULLING) snprintf(m, sizeof(m), _("SYNC_STATE_PULL"), st.pct);
    else if (run) strlcpy(m, _("SYNC_STATE_RUN"), sizeof(m));
    else if (!fd_net_is_connected()) { strlcpy(m, _("SYNC_STATE_IDLE"), sizeof(m)); col = FD_COLOR_ALERT; }
    else if (st.state == FD_SYNC_ERROR && st.err[0]) { fmt_err(st.err, m, sizeof(m)); col = 0xDC2626; }
    else if (st.state == FD_SYNC_OK) { strlcpy(m, _("SYNC_STATE_OK"), sizeof(m)); col = 0x16A34A; }
    else strlcpy(m, _("SYNC_AUTO_HINT"), sizeof(m));
    lv_obj_set_style_text_color(s_state, lv_color_hex(col), 0);
    lv_label_set_text(s_state, m);
    if (run) lv_obj_add_state(s_btn, LV_STATE_DISABLED);
    else lv_obj_remove_state(s_btn, LV_STATE_DISABLED);
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_t *t = lv_label_create(s_body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_label_set_text(t, _("SYNC"));
    s_v[0] = kv(s_body, _("SYNC_WORDS"));
    s_v[1] = kv(s_body, _("SYNC_SENTENCES"));
    s_v[2] = kv(s_body, _("SYNC_COLLECTIONS"));
    s_v[3] = kv(s_body, _("SYNC_LAST"));
    s_v[4] = kv(s_body, _("SYNC_PENDING"));
    s_state = lv_label_create(s_body);
    lv_obj_set_width(s_state, lv_pct(100));
    lv_label_set_long_mode(s_state, LV_LABEL_LONG_WRAP);
    lv_obj_t *r = lv_obj_create(s_body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 44);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 8, 0);
    if (!fd_net_is_connected()) ui_gate_btn(r, _("OPEN_WIFI"), false, wifi_cb);
    s_btn = ui_gate_btn(r, _("SYNC_NOW"), true, now_cb);
    refresh();
    /* Vao man la dong bo ngay (giong nut Dong bo ngay), neu da co Wi-Fi va chua dang chay. */
    if (fd_net_is_connected()) {
        fd_sync_status_t st;
        fd_sync_get(&st);
        bool run = st.state == FD_SYNC_CHECKING || st.state == FD_SYNC_PULLING || st.state == FD_SYNC_PUSHING || st.manual;
        if (!run) fd_sync_force();
    }
}

static void destroy(void) { s_body = s_state = s_btn = NULL; memset(s_v, 0, sizeof(s_v)); }
static void on_tick(void) { refresh(); }

const ui_screen_t g_scr_sync = {.name = "sync", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = on_tick};
