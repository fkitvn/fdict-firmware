/* 2.2.2 Che do nha phat trien > Thu Wi-Fi: SSID / RSSI (cap nhat moi giay) / IP; [Bat dau] chay 60 s: moi ~1 s gui
 * HTTP HEAD toi may chu dang dung (fd_ota_base_url(), khong token, khong log URL) trong task rieng.
 * Thanh cong = co phan hoi HTTP (bat ky ma), loi = timeout / loi mang. Dem do tre TB/max, so lan mat Wi-Fi (fd_net_dbg). */
#include <stdio.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "ui_priv.h"

#define WT_RUN_MS      60000
#define WT_PERIOD_MS   1000
#define WT_TIMEOUT_MS  5000

static const char *TAG = "wifitest";

typedef struct {
    uint32_t ok, fail, disc;
    uint32_t sum_ms, max_ms, last_ms;
    int last_code;
    int64_t t0_us, t_end_us;
} wt_stats_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static wt_stats_t s_st;
static volatile bool s_run;        /* yeu cau chay */
static volatile bool s_task_alive; /* task dang ton tai */
static uint32_t s_disc0;

static lv_obj_t *s_info, *s_stats, *s_state, *s_btn_lbl;
static lv_timer_t *s_tm;

static void wt_task(void *arg)
{
    (void)arg;
    esp_http_client_handle_t cl = NULL;
    const char *base = NULL;
    while (s_run) {
        int64_t now = esp_timer_get_time();
        if ((now - s_st.t0_us) / 1000 >= WT_RUN_MS) break;
        int64_t t1 = now;
        int code = -1;
        if (fd_net_is_connected()) {
            const char *b = fd_ota_base_url();
            if (!cl || b != base) {
                if (cl) esp_http_client_cleanup(cl);
                base = b;
                esp_http_client_config_t c = {
                    .url = b,
                    .method = HTTP_METHOD_HEAD,
                    .timeout_ms = WT_TIMEOUT_MS,
                    .crt_bundle_attach = esp_crt_bundle_attach,
                    .keep_alive_enable = true,
                    .disable_auto_redirect = true,
                };
                cl = esp_http_client_init(&c);
            }
            if (cl && esp_http_client_perform(cl) == ESP_OK) code = esp_http_client_get_status_code(cl);
            else if (cl) { esp_http_client_cleanup(cl); cl = NULL; }   /* loi: tao ket noi moi lan sau */
        }
        uint32_t ms = (uint32_t)((esp_timer_get_time() - t1) / 1000);
        fd_net_dbg_t d;
        fd_net_dbg_get(&d);
        taskENTER_CRITICAL(&s_mux);
        if (code > 0) {
            s_st.ok++;
            s_st.sum_ms += ms;
            if (ms > s_st.max_ms) s_st.max_ms = ms;
            s_st.last_ms = ms;
        } else s_st.fail++;
        s_st.last_code = code;
        s_st.disc = d.disc_count - s_disc0;
        taskEXIT_CRITICAL(&s_mux);
        if (ms < WT_PERIOD_MS) vTaskDelay(pdMS_TO_TICKS(WT_PERIOD_MS - ms));
        else vTaskDelay(1);
    }
    if (cl) esp_http_client_cleanup(cl);
    taskENTER_CRITICAL(&s_mux);
    s_st.t_end_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "ket thuc: ok=%u fail=%u disc=%u max=%u ms", (unsigned)s_st.ok, (unsigned)s_st.fail,
             (unsigned)s_st.disc, (unsigned)s_st.max_ms);
    s_run = false;
    s_task_alive = false;
    vTaskDelete(NULL);
}

static void refresh(void)
{
    if (!s_info) return;
    char ssid[33] = "", ip[20] = "";
    bool con = fd_net_is_connected();
    if (con) {
        fd_net_ssid(ssid, sizeof(ssid));
        fd_net_ip(ip, sizeof(ip));
        lv_label_set_text_fmt(s_info, "SSID: %s\nRSSI: %d dBm\nIP: %s", ssid, fd_net_rssi(), ip);
    } else lv_label_set_text_fmt(s_info, "SSID: -\nRSSI: -\nIP: -\n%s", _("NOT_CONNECTED"));

    wt_stats_t st;
    taskENTER_CRITICAL(&s_mux);
    st = s_st;
    taskEXIT_CRITICAL(&s_mux);
    bool alive = s_task_alive;
    if (st.t0_us) {
        int64_t end = alive ? esp_timer_get_time() : st.t_end_us;
        unsigned el = (unsigned)((end - st.t0_us) / 1000000);
        unsigned avg = st.ok ? (unsigned)(st.sum_ms / st.ok) : 0;
        lv_label_set_text_fmt(s_stats, _("WT_STATS"), el, WT_RUN_MS / 1000, (unsigned)st.ok, (unsigned)st.fail,
                              avg, (unsigned)st.max_ms, (unsigned)st.disc);
        if (alive) lv_label_set_text(s_state, _("WT_RUNNING"));
        else if (st.fail == 0 && st.disc == 0 && st.ok > 0) lv_label_set_text(s_state, _("WT_PASS"));
        else lv_label_set_text(s_state, _("WT_DONE"));
    } else {
        lv_label_set_text(s_stats, "");
        lv_label_set_text(s_state, _("WT_IDLE"));
    }
    lv_label_set_text(s_btn_lbl, alive ? _("WT_STOP") : _("WT_START"));
}

static void tm_cb(lv_timer_t *t) { (void)t; refresh(); }

static void start_stop_cb(lv_event_t *e)
{
    (void)e;
    if (s_task_alive) { s_run = false; refresh(); return; }   /* task tu thoat sau request hien tai */
    if (!fd_net_is_connected()) { fd_ui_toast(_("NOT_CONNECTED")); return; }
    fd_net_dbg_t d;
    fd_net_dbg_get(&d);
    s_disc0 = d.disc_count;
    taskENTER_CRITICAL(&s_mux);
    s_st = (wt_stats_t){0};
    s_st.t0_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_mux);
    s_run = true;
    s_task_alive = true;
    if (xTaskCreate(wt_task, "fd_wifitest", 6144, NULL, 4, NULL) != pdPASS) {
        s_run = s_task_alive = false;
        s_st.t0_us = 0;
        fd_ui_toast(_("WT_FAILED"));
    }
    refresh();
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_all(body, 10, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_scrollable(body, true);

    lv_obj_t *t = mk_label(body, &fd_font_noto_18);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("WIFI_TEST"));
    s_info = mk_label(body, &fd_font_noto_14);
    s_state = mk_label(body, &fd_font_noto_18);
    lv_obj_set_style_text_color(s_state, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    s_stats = mk_label(body, &fd_font_noto_14);

    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, lv_pct(100), 44);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    s_btn_lbl = lv_label_create(b);
    lv_obj_center(s_btn_lbl);
    lv_obj_add_event_cb(b, start_stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *h = mk_label(body, &fd_font_noto_14);
    lv_obj_set_style_text_color(h, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(h, _("WT_HINT"));

    s_tm = lv_timer_create(tm_cb, 1000, NULL);
    refresh();
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    s_run = false;   /* roi man: dung bai thu (task tu thoat) */
    s_info = s_stats = s_state = s_btn_lbl = NULL;
}

const ui_screen_t g_scr_wifitest = {
    .name = "wifitest", .parent = FD_SCR_DEVMODE, .build = build, .destroy = destroy,
};
