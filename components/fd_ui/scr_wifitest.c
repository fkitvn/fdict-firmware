/* 2.2.2 Che do nha phat trien > Thu Wi-Fi. 2.2.3: giao dien the (header, the tin hieu + vach RSSI, vong tien do 60 s,
 * luoi the so lieu, bang ket luan mau: xanh on dinh / vang khong on dinh / do loi) + RSSI min/TB/max.
 * [Bat dau] chay 60 s: moi ~1 s gui HTTP HEAD toi may chu dang dung (fd_ota_base_url(), khong token, khong log URL) trong
 * task rieng. Thanh cong = co phan hoi HTTP (bat ky ma), loi = timeout / loi mang. Mat ket noi: fd_net_dbg.
 * Ket qua cuoi gom trong wt_result_t; wt_on_finished() (task bai thu, ngoai LVGL) log + gui len may chu (2.2.7). */
#include <stdio.h>
#include <string.h>
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
#define WT_SLOW_MS     1500    /* do tre max > nguong -> khong on dinh */

static const char *TAG = "wifitest";

typedef enum { WT_V_NONE = 0, WT_V_STABLE, WT_V_UNSTABLE, WT_V_FAILED } wt_verdict_t;

typedef struct {
    uint32_t ok, fail, disc;
    uint32_t sum_ms, max_ms;
    int rssi_min, rssi_max, rssi_n;
    int32_t rssi_sum;
    int64_t t0_us, t_end_us;
    bool aborted;              /* nguoi dung dung som */
    wt_verdict_t verdict;      /* tinh khi ket thuc */
} wt_result_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static wt_result_t s_st;
static volatile bool s_run;        /* yeu cau chay */
static volatile bool s_task_alive; /* task dang ton tai */
static uint32_t s_disc0;

static wt_verdict_t verdict_of(const wt_result_t *r)
{
    uint32_t n = r->ok + r->fail;
    if (!n) return WT_V_NONE;
    if (!r->ok || r->fail * 5 > n) return WT_V_FAILED;           /* > 20% loi */
    if (r->fail || r->disc || r->max_ms > WT_SLOW_MS) return WT_V_UNSTABLE;
    return WT_V_STABLE;
}

/* 2.2.7: gui ket qua: POST {base}/api/devices/diag/wifi (fd_ota_api_post_ex: Bearer device_token neu da ghep, khong log
 * token/URL). Chay trong task bai thu. Trang thai cho UI: s_up_state + s_up_msg. */
enum { UP_NONE = 0, UP_SENDING, UP_OK, UP_ERR, UP_SKIP };
static volatile int s_up_state;
static char s_up_msg[64];
static char s_ssid_run[33], s_ip_run[20];

static void json_str(char *out, size_t cap, const char *in)   /* chuoi JSON an toan (co dau ngoac kep) */
{
    size_t o = 0;
    if (cap < 3) { if (cap) out[0] = 0; return; }
    out[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < cap; p++) {
        if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = (char)*p; }
        else if (*p < 0x20) o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *p);
        else out[o++] = (char)*p;
    }
    out[o++] = '"';
    out[o] = 0;
}

static void msg_of(const char *resp, char *out, size_t cap)   /* lay "message" tu body loi, neu co */
{
    out[0] = 0;
    const char *m = resp ? strstr(resp, "\"message\"") : NULL;
    if (!m || !(m = strchr(m + 9, '"'))) return;
    m++;
    size_t o = 0;
    while (*m && *m != '"' && o + 1 < cap) { if (*m == '\\' && m[1]) m++; out[o++] = *m++; }
    out[o] = 0;
}

static void wt_upload(const wt_result_t *r)
{
    if (!r->ok && !r->fail) { s_up_state = UP_SKIP; return; }
    s_up_state = UP_SENDING;
    char id[16], ssid[80], body[512];
    static char resp[256];
    fd_ota_get_device_id(id, sizeof(id));
    json_str(ssid, sizeof(ssid), s_ssid_run);
    int rmin = r->rssi_n ? r->rssi_min : -127, rmax = r->rssi_n ? r->rssi_max : -127;
    int ravg = r->rssi_n ? (int)(r->rssi_sum / r->rssi_n) : -127;
    if (ravg < rmin) ravg = rmin;
    if (ravg > rmax) ravg = rmax;
    unsigned dur = (unsigned)((r->t_end_us - r->t0_us + 500000) / 1000000);
    if (dur < 1) dur = 1;
    if (dur > 3600) dur = 3600;
    char lat[48];
    if (r->ok) snprintf(lat, sizeof(lat), "{\"avg\":%u,\"max\":%u}", (unsigned)(r->sum_ms / r->ok), (unsigned)r->max_ms);
    else snprintf(lat, sizeof(lat), "null");
    char ip[40] = "";
    if (s_ip_run[0]) snprintf(ip, sizeof(ip), ",\"ip\":\"%s\"", s_ip_run);
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"ssid\":%s,\"rssi\":{\"min\":%d,\"avg\":%d,\"max\":%d},\"duration_s\":%u,"
             "\"success\":%u,\"fail\":%u,\"latency_ms\":%s,\"disconnects\":%u,\"fw_version\":\"%s\"%s}",
             id, ssid, rmin, ravg, rmax, dur, (unsigned)r->ok, (unsigned)r->fail, lat,
             (unsigned)(r->disc > 10000 ? 10000 : r->disc), FD_FW_SEMVER, ip);
    resp[0] = 0;
    int code = fd_ota_api_post_ex("/api/devices/diag/wifi", body, resp, sizeof(resp), false);
    ESP_LOGI(TAG, "gui ket qua: HTTP %d", code);
    if (code == 200) { s_up_state = UP_OK; return; }
    char m[48];
    msg_of(resp, m, sizeof(m));
    if (m[0]) snprintf(s_up_msg, sizeof(s_up_msg), "%s", m);
    else if (code < 0) snprintf(s_up_msg, sizeof(s_up_msg), "%s", "network");
    else snprintf(s_up_msg, sizeof(s_up_msg), "HTTP %d", code);
    s_up_state = UP_ERR;
}

/* Ket thuc bai thu: log + gui ket qua len may chu. Goi tu task bai thu, khong giu lock LVGL. */
static void wt_on_finished(const wt_result_t *r)
{
    ESP_LOGI(TAG, "ket qua: verdict=%d ok=%u fail=%u disc=%u avg=%u max=%u ms rssi=%d/%d/%d%s", (int)r->verdict,
             (unsigned)r->ok, (unsigned)r->fail, (unsigned)r->disc, r->ok ? (unsigned)(r->sum_ms / r->ok) : 0u,
             (unsigned)r->max_ms, r->rssi_min, r->rssi_n ? (int)(r->rssi_sum / r->rssi_n) : 0, r->rssi_max,
             r->aborted ? " (dung som)" : "");
    wt_upload(r);
}

static void wt_task(void *arg)
{
    (void)arg;
    esp_http_client_handle_t cl = NULL;
    const char *base = NULL;
    while (s_run) {
        int64_t now = esp_timer_get_time();
        if ((now - s_st.t0_us) / 1000 >= WT_RUN_MS) break;
        int code = -1;
        bool con = fd_net_is_connected();
        int rssi = con ? fd_net_rssi() : 0;
        if (con) {
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
        uint32_t ms = (uint32_t)((esp_timer_get_time() - now) / 1000);
        fd_net_dbg_t d;
        fd_net_dbg_get(&d);
        taskENTER_CRITICAL(&s_mux);
        if (code > 0) {
            s_st.ok++;
            s_st.sum_ms += ms;
            if (ms > s_st.max_ms) s_st.max_ms = ms;
        } else s_st.fail++;
        if (con && rssi < 0) {
            if (!s_st.rssi_n || rssi < s_st.rssi_min) s_st.rssi_min = rssi;
            if (!s_st.rssi_n || rssi > s_st.rssi_max) s_st.rssi_max = rssi;
            s_st.rssi_sum += rssi;
            s_st.rssi_n++;
        }
        s_st.disc = d.disc_count - s_disc0;
        taskEXIT_CRITICAL(&s_mux);
        if (ms < WT_PERIOD_MS) vTaskDelay(pdMS_TO_TICKS(WT_PERIOD_MS - ms));
        else vTaskDelay(1);
    }
    if (cl) esp_http_client_cleanup(cl);
    wt_result_t r;
    taskENTER_CRITICAL(&s_mux);
    s_st.t_end_us = esp_timer_get_time();
    s_st.aborted = !s_run;
    s_st.verdict = verdict_of(&s_st);
    r = s_st;
    taskEXIT_CRITICAL(&s_mux);
    wt_on_finished(&r);
    s_run = false;
    s_task_alive = false;
    vTaskDelete(NULL);
}

/* ---------- Giao dien ---------- */
enum { C_OK, C_FAIL, C_AVG, C_MAX, C_DISC, C_RSSI, C_N };
static lv_obj_t *s_ssid, *s_rssi, *s_ip, *s_bars[4], *s_arc, *s_arc_lbl, *s_banner, *s_banner_lbl, *s_btn, *s_btn_lbl;
static lv_obj_t *s_val[C_N], *s_up_lbl;
static lv_timer_t *s_tm;

static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_style_bg_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_set_scrollable(c, false);
    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    return l;
}

static uint32_t rssi_colour(int r)
{
    return r >= -60 ? 0x16A34A : r >= -70 ? 0x65A30D : r >= -80 ? 0xD97706 : 0xDC2626;
}

static void refresh(void)
{
    if (!s_ssid) return;
    bool con = fd_net_is_connected();
    int rssi = con ? fd_net_rssi() : 0;
    if (con) {
        char ssid[33] = "", ip[20] = "";
        fd_net_ssid(ssid, sizeof(ssid));
        fd_net_ip(ip, sizeof(ip));
        lv_label_set_text(s_ssid, ssid);
        lv_label_set_text_fmt(s_ip, "IP %s", ip);
        lv_label_set_text_fmt(s_rssi, "%d dBm", rssi);
    } else {
        lv_label_set_text(s_ssid, _("NOT_CONNECTED"));
        lv_label_set_text(s_ip, "IP -");
        lv_label_set_text(s_rssi, "-- dBm");
    }
    int lv = !con ? 0 : rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
    uint32_t rc = con ? rssi_colour(rssi) : 0x9CA3AF;
    lv_obj_set_style_text_color(s_rssi, lv_color_hex(rc), 0);
    for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(i < lv ? rc : FD_COLOR_BORDER), 0);

    wt_result_t st;
    taskENTER_CRITICAL(&s_mux);
    st = s_st;
    taskEXIT_CRITICAL(&s_mux);
    bool alive = s_task_alive;
    unsigned el = 0;
    if (st.t0_us) {
        int64_t end = alive ? esp_timer_get_time() : st.t_end_us;
        el = (unsigned)((end - st.t0_us) / 1000000);
        if (el > WT_RUN_MS / 1000) el = WT_RUN_MS / 1000;
    }
    lv_arc_set_value(s_arc, (int32_t)el);
    lv_label_set_text_fmt(s_arc_lbl, "%u s", el);

    if (st.t0_us) {
        lv_label_set_text_fmt(s_val[C_OK], "%u", (unsigned)st.ok);
        lv_label_set_text_fmt(s_val[C_FAIL], "%u", (unsigned)st.fail);
        lv_label_set_text_fmt(s_val[C_AVG], "%u ms", st.ok ? (unsigned)(st.sum_ms / st.ok) : 0u);
        lv_label_set_text_fmt(s_val[C_MAX], "%u ms", (unsigned)st.max_ms);
        lv_label_set_text_fmt(s_val[C_DISC], "%u", (unsigned)st.disc);
        if (st.rssi_n) lv_label_set_text_fmt(s_val[C_RSSI], "%d/%d/%d", st.rssi_min, (int)(st.rssi_sum / st.rssi_n), st.rssi_max);
        else lv_label_set_text(s_val[C_RSSI], "-");
    } else for (int i = 0; i < C_N; i++) lv_label_set_text(s_val[i], "-");

    uint32_t bg = 0xF3F4F6, fg = FD_COLOR_TEXT_MUTED;
    const char *txt = _("WT_IDLE");
    if (alive) { bg = FD_COLOR_PRIMARY_TINT; fg = FD_COLOR_PRIMARY_TEXT; txt = _("WT_RUNNING"); }
    else if (st.t0_us) switch (st.verdict) {
        case WT_V_STABLE:   bg = 0xDCFCE7; fg = 0x15803D; txt = _("WT_V_STABLE"); break;
        case WT_V_UNSTABLE: bg = 0xFEF3C7; fg = 0xB45309; txt = _("WT_V_UNSTABLE"); break;
        case WT_V_FAILED:   bg = 0xFEE2E2; fg = 0xB91C1C; txt = _("WT_V_FAILED"); break;
        default: break;
    }
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(s_banner_lbl, lv_color_hex(fg), 0);
    lv_label_set_text(s_banner_lbl, txt);
    switch (alive ? UP_NONE : s_up_state) {
        case UP_SENDING: lv_label_set_text(s_up_lbl, _("WT_UP_SENDING")); break;
        case UP_OK:      lv_label_set_text(s_up_lbl, _("WT_UP_OK")); break;
        case UP_ERR:     lv_label_set_text_fmt(s_up_lbl, _("WT_UP_ERR"), s_up_msg); break;
        default:         lv_label_set_text(s_up_lbl, ""); break;
    }
    bool sending = s_up_state == UP_SENDING;
    if (sending) lv_obj_add_state(s_btn, LV_STATE_DISABLED);
    else lv_obj_remove_state(s_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_btn_lbl, alive ? _("WT_STOP") : _("WT_START"));
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(alive ? 0xDC2626 : FD_COLOR_PRIMARY), 0);
}

static void tm_cb(lv_timer_t *t) { (void)t; refresh(); }

static void start_stop_cb(lv_event_t *e)
{
    (void)e;
    if (s_task_alive) { s_run = false; refresh(); return; }   /* task tu thoat sau request hien tai */
    if (!fd_net_is_connected()) { fd_ui_toast(_("NOT_CONNECTED")); return; }
    if (s_up_state == UP_SENDING) return;
    s_up_state = UP_NONE;
    fd_net_ssid(s_ssid_run, sizeof(s_ssid_run));
    fd_net_ip(s_ip_run, sizeof(s_ip_run));
    fd_net_dbg_t d;
    fd_net_dbg_get(&d);
    s_disc0 = d.disc_count;
    taskENTER_CRITICAL(&s_mux);
    s_st = (wt_result_t){0};
    s_st.t0_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_mux);
    s_run = true;
    s_task_alive = true;
    if (xTaskCreate(wt_task, "fd_wifitest", 8192, NULL, 4, NULL) != pdPASS) {
        s_run = s_task_alive = false;
        s_st.t0_us = 0;
        fd_ui_toast(_("WT_FAILED"));
    }
    refresh();
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_scrollable(body, true);

    /* Header: tieu de + may chu */
    lv_obj_t *t = label(body, &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_set_width(t, lv_pct(100)); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("WIFI_TEST"));
    lv_obj_t *sv = label(body, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(sv, lv_pct(100)); lv_obj_set_style_text_align(sv, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(sv, _("WT_SERVER"), fd_ota_is_lab() ? "Lab" : _("WT_OFFICIAL"));

    /* The tin hieu: vach | SSID + IP | RSSI */
    lv_obj_t *sig = card(body);
    lv_obj_set_size(sig, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sig, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sig, 10, 0);
    lv_obj_t *bars = lv_obj_create(sig);
    lv_obj_remove_style_all(bars);
    lv_obj_set_size(bars, 34, 28);
    lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(bars, 2, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = s_bars[i] = lv_obj_create(bars);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 6, 7 + i * 7);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    }
    lv_obj_t *mid = lv_obj_create(sig);
    lv_obj_remove_style_all(mid);
    lv_obj_set_height(mid, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    s_ssid = label(mid, &fd_font_noto_14, FD_COLOR_TEXT);
    lv_obj_set_width(s_ssid, lv_pct(100)); lv_label_set_long_mode(s_ssid, LV_LABEL_LONG_DOT);
    s_ip = label(mid, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    s_rssi = label(sig, &fd_font_noto_18, FD_COLOR_TEXT);

    /* Vong tien do 60 s + bang ket luan */
    lv_obj_t *mrow = lv_obj_create(body);
    lv_obj_remove_style_all(mrow);
    lv_obj_set_size(mrow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mrow, 10, 0);
    s_arc = lv_arc_create(mrow);
    lv_obj_set_size(s_arc, 84, 84);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, WT_RUN_MS / 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(s_arc, false);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(FD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(FD_COLOR_PRIMARY), LV_PART_INDICATOR);
    s_arc_lbl = label(s_arc, &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_center(s_arc_lbl);
    lv_obj_t *rcol = lv_obj_create(mrow);
    lv_obj_remove_style_all(rcol);
    lv_obj_set_height(rcol, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(rcol, 1);
    lv_obj_set_flex_flow(rcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rcol, 6, 0);
    s_banner = lv_obj_create(rcol);
    lv_obj_remove_style_all(s_banner);
    lv_obj_set_size(s_banner, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_banner, 10, 0);
    lv_obj_set_style_pad_all(s_banner, 8, 0);
    s_banner_lbl = label(s_banner, &fd_font_noto_14, FD_COLOR_TEXT);
    lv_obj_set_width(s_banner_lbl, lv_pct(100)); lv_label_set_long_mode(s_banner_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_banner_lbl, LV_TEXT_ALIGN_CENTER, 0);
    s_btn = lv_button_create(rcol);
    lv_obj_set_size(s_btn, lv_pct(100), 38);
    lv_obj_set_style_radius(s_btn, 10, 0);
    lv_obj_set_style_shadow_width(s_btn, 0, 0);
    s_btn_lbl = lv_label_create(s_btn);
    lv_obj_center(s_btn_lbl);
    lv_obj_add_event_cb(s_btn, start_stop_cb, LV_EVENT_CLICKED, NULL);
    s_up_lbl = label(rcol, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(s_up_lbl, lv_pct(100)); lv_label_set_long_mode(s_up_lbl, LV_LABEL_LONG_WRAP);

    /* Luoi the so lieu 2 cot */
    static const char *const K[C_N] = {"WT_C_OK", "WT_C_FAIL", "WT_C_AVG", "WT_C_MAX", "WT_C_DISC", "WT_C_RSSI"};
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 6, 0);
    for (int i = 0; i < C_N; i++) {
        lv_obj_t *c = card(grid);
        lv_obj_set_size(c, lv_pct(48), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_t *k = label(c, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
        lv_obj_set_width(k, lv_pct(100)); lv_label_set_long_mode(k, LV_LABEL_LONG_DOT);
        lv_label_set_text(k, _(K[i]));
        uint32_t col = i == C_OK ? 0x15803D : i == C_FAIL ? 0xB91C1C : FD_COLOR_TEXT;
        s_val[i] = label(c, &fd_font_noto_18, col);
    }
    lv_obj_t *h = label(body, &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_label_set_text(h, _("WT_HINT"));

    s_tm = lv_timer_create(tm_cb, 1000, NULL);
    refresh();
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    s_run = false;   /* roi man: dung bai thu (task tu thoat) */
    s_up_lbl = NULL;
    s_ssid = s_rssi = s_ip = s_arc = s_arc_lbl = s_banner = s_banner_lbl = s_btn = s_btn_lbl = NULL;
    for (int i = 0; i < 4; i++) s_bars[i] = NULL;
    for (int i = 0; i < C_N; i++) s_val[i] = NULL;
}

const ui_screen_t g_scr_wifitest = {
    .name = "wifitest", .parent = FD_SCR_DEVMODE, .build = build, .destroy = destroy,
};
