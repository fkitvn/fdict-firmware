/* Buoc 2: Lien ket Zalo (xem fd_zlink.h). Khong log ma lien ket. */
#include "fd_zlink.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fd_net.h"
#include "fd_ota.h"

static const char *TAG = "fd_zlink";
#define BIT_STATUS 0x1
#define BIT_START 0x2
#define BIT_POLL 0x4     /* 2.1.2: tiep tuc poll ma con han (khong xin ma moi) */
#define REUSE_MIN_S 60   /* ma con >= 60 s thi dung lai */
#define POLL_MS 4000
#define RESP_CAP 384

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static volatile int s_linked = FD_ZL_UNKNOWN;
static volatile uint32_t s_gen, s_srv_gen;
static fd_zlink_code_t s_code;
static int64_t s_code_deadline_us;
static volatile uint32_t s_code_seq;
static volatile bool s_code_polling;
static volatile int s_warr = -1;
static char s_warr_exp[32];

/* ngay dan su <-> so ngay (Howard Hinnant) */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static void set_warranty(int v, const char *resp)
{
    char ex[32] = "";
    const char *p = resp ? strstr(resp, "\"warranty_expires_at\":\"") : NULL;
    int y, mo, d, h;
    if (p && sscanf(p + 23, "%4d-%2d-%2dT%2d", &y, &mo, &d, &h) == 4) {
        long z = days_from_civil(y, mo, d) + (h + 7 >= 24 ? 1 : 0);   /* UTC -> gio VN (UTC+7) */
        civil_from_days(z, &y, &mo, &d);
        snprintf(ex, sizeof(ex), "%02d/%02d/%04d", d, mo, y);
    }
    taskENTER_CRITICAL(&s_mux);
    if (s_warr != v || strcmp(s_warr_exp, ex)) { s_warr = v; memcpy(s_warr_exp, ex, sizeof(ex)); s_gen++; }
    taskEXIT_CRITICAL(&s_mux);
}   /* tang khi start/stop -> vong poll cu tu dung */

static void set_linked(int v)
{
    taskENTER_CRITICAL(&s_mux);
    if (s_linked != v) { s_linked = v; s_gen++; }
    taskEXIT_CRITICAL(&s_mux);
}

static void set_code(fd_zlink_code_state_t st, const char *code, int expires_s, const char *err)
{
    taskENTER_CRITICAL(&s_mux);
    s_code.state = st;
    if (code) { strncpy(s_code.code, code, sizeof(s_code.code) - 1); s_code.code[sizeof(s_code.code) - 1] = 0; }
    else if (st != FD_ZLC_CODE) memset(s_code.code, 0, sizeof(s_code.code));
    if (expires_s >= 0) s_code_deadline_us = esp_timer_get_time() + (int64_t)expires_s * 1000000;
    strncpy(s_code.err, err ? err : "", sizeof(s_code.err) - 1);
    s_code.err[sizeof(s_code.err) - 1] = 0;
    s_gen++;
    taskEXIT_CRITICAL(&s_mux);
}

static bool json_true(const char *s, const char *key)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":true", key);
    return s && strstr(s, pat);
}

static int json_int(const char *s, const char *key, int def)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = s ? strstr(s, pat) : NULL;
    return p ? atoi(p + strlen(pat)) : def;
}

static bool json_code(const char *s, char *out)
{
    const char *p = s ? strstr(s, "\"code\":\"") : NULL;
    if (!p) return false;
    p += 8;
    for (int i = 0; i < 6; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
        out[i] = p[i];
    }
    out[6] = 0;
    return p[6] == '"';
}

static int call(const char *path, char *resp)
{
    char id[16], body[48];
    fd_ota_get_device_id(id, sizeof(id));
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", id);
    return fd_ota_api_post_ex(path, body, resp, RESP_CAP, false);
}

static bool wait_wifi(int sec)
{
    for (int i = 0; i < sec * 2 && !fd_net_is_connected(); i++) vTaskDelay(pdMS_TO_TICKS(500));
    return fd_net_is_connected();
}

static void do_status(void)
{
    if (!wait_wifi(90)) return;
    char *resp = malloc(RESP_CAP);
    if (!resp) return;
    /* Chua dang nhap (ke ca sau khoi phuc cai dat goc): van doc han bao hanh theo MAC. Ban ghi nam o may chu. */
    if (!fd_ota_is_paired()) {
        set_linked(FD_ZL_UNKNOWN);
        int code = call("/api/devices/warranty/lookup", resp);
        if (code == 200) set_warranty(json_true(resp, "warranty_activated") ? 1 : 0, resp);
        ESP_LOGI(TAG, "lookup -> %d", code);
        free(resp);
        return;
    }
    int code = call("/api/devices/zalo-link/status", resp);
    if (code == 200) {
        set_linked(json_true(resp, "zalo_linked") ? FD_ZL_YES : FD_ZL_NO);
        set_warranty(json_true(resp, "warranty_activated") ? 1 : 0, resp);
    } else if (code == 401) { set_linked(FD_ZL_UNKNOWN); set_warranty(-1, NULL); }
    ESP_LOGI(TAG, "status -> %d", code);
    free(resp);
}

static void poll_loop(uint32_t seq, char *resp);

static void do_code(uint32_t seq)
{
    char *resp = malloc(RESP_CAP);
    if (!resp) { set_code(FD_ZLC_ERROR, NULL, -1, "nomem"); return; }
    set_code(FD_ZLC_REQUESTING, NULL, -1, NULL);
    if (!wait_wifi(5)) { set_code(FD_ZLC_ERROR, NULL, -1, "no_wifi"); goto out; }
    int64_t t0 = esp_timer_get_time();
    int code = call("/api/devices/zalo-link/start", resp);
    ESP_LOGI(TAG, "start -> %d (%d ms)", code, (int)((esp_timer_get_time() - t0) / 1000));   /* khong log body (co ma) */
    if (seq != s_code_seq) goto out;
    if (code == 200 && json_true(resp, "linked")) { do_status(); set_code(FD_ZLC_DONE, NULL, -1, NULL); goto out; }
    char c[8];
    if (code != 200 || !json_code(resp, c)) {
        char e[20];
        snprintf(e, sizeof(e), code < 0 ? "net" : "http_%d", code);
        set_code(FD_ZLC_ERROR, NULL, -1, e);
        goto out;
    }
    set_code(FD_ZLC_CODE, c, json_int(resp, "expires_in", 300), NULL);
    set_linked(FD_ZL_NO);   /* start cap ma = chua kich hoat bao hanh -> khoi goi status rieng */
    memset(c, 0, sizeof(c));
    poll_loop(seq, resp);
out:
    free(resp);
}

static void do_poll(uint32_t seq)
{
    char *resp = malloc(RESP_CAP);
    if (!resp) return;
    poll_loop(seq, resp);
    free(resp);
}

static void poll_loop(uint32_t seq, char *resp)
{
    memset(resp, 0, RESP_CAP);
    while (seq == s_code_seq) {
        for (int w = 0; w < POLL_MS / 250 && seq == s_code_seq; w++) vTaskDelay(pdMS_TO_TICKS(250));   /* 2.1.2: thoat nhanh khi xin ma moi */
        if (seq != s_code_seq) break;
        if (esp_timer_get_time() >= s_code_deadline_us) { set_code(FD_ZLC_EXPIRED, NULL, -1, NULL); break; }
        if (!fd_net_is_connected()) continue;
        int pc = call("/api/devices/zalo-link/poll", resp);
        if (seq != s_code_seq) break;
        if (pc == 200 && json_true(resp, "linked")) {
            do_status();
            set_code(FD_ZLC_DONE, NULL, -1, NULL);
            break;
        }
        if (pc == 410 || pc == 404) { set_code(FD_ZLC_EXPIRED, NULL, -1, NULL); break; }
        if (pc == 401) { set_linked(FD_ZL_UNKNOWN); set_code(FD_ZLC_ERROR, NULL, -1, "unpaired"); break; }
    }
    if (seq == s_code_seq) s_code_polling = false;
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (bits & BIT_START) do_code(s_code_seq);
        else if (bits & BIT_POLL) do_poll(s_code_seq);
        else if (bits & BIT_STATUS) do_status();
    }
}

static void kick(uint32_t bit)
{
    if (!s_task) {
        taskENTER_CRITICAL(&s_mux);
        bool create = !s_task;
        taskEXIT_CRITICAL(&s_mux);
        if (create && xTaskCreate(task, "fd_zlink", 8192, NULL, 3, &s_task) != pdPASS) {
            ESP_LOGW(TAG, "khong tao duoc task");
            s_task = NULL;
            return;
        }
    }
    xTaskNotify(s_task, bit, eSetBits);
}

void fd_zlink_refresh(void)
{
    if (s_srv_gen != fd_ota_server_gen()) { s_srv_gen = fd_ota_server_gen(); set_linked(FD_ZL_UNKNOWN); set_warranty(-1, NULL); }
    kick(BIT_STATUS);
}

fd_zlink_linked_t fd_zlink_linked(void)
{
    if (!fd_ota_is_paired() || s_srv_gen != fd_ota_server_gen()) return FD_ZL_UNKNOWN;
    return (fd_zlink_linked_t)s_linked;
}

uint32_t fd_zlink_gen(void) { return s_gen; }

int fd_zlink_warranty(char *out, size_t n)
{
    if (s_srv_gen != fd_ota_server_gen()) return -1;
    taskENTER_CRITICAL(&s_mux);
    int v = s_warr;
    if (out && n) { strncpy(out, s_warr_exp, n - 1); out[n - 1] = 0; }
    taskEXIT_CRITICAL(&s_mux);
    return v;
}

void fd_zlink_code_start(void)
{
    s_code_seq++;
    s_code_polling = true;
    set_code(FD_ZLC_REQUESTING, NULL, -1, NULL);
    kick(BIT_START);
}

/* 2.1.2: ma con han -> dung lai + poll tiep; dang xin -> doi; con lai -> xin ma moi (1 request start, khong goi status truoc). */
void fd_zlink_code_ensure(void)
{
    taskENTER_CRITICAL(&s_mux);
    fd_zlink_code_state_t st = s_code.state;
    int64_t left = (s_code_deadline_us - esp_timer_get_time()) / 1000000;
    bool polling = s_code_polling;
    taskEXIT_CRITICAL(&s_mux);
    if (st == FD_ZLC_REQUESTING) return;
    if (st == FD_ZLC_CODE && left >= REUSE_MIN_S) {
        if (!polling) { s_code_seq++; s_code_polling = true; kick(BIT_POLL); }
        return;
    }
    fd_zlink_code_start();
}

void fd_zlink_code_stop(void)
{
    s_code_seq++;   /* dung poll; giu ma con han trong RAM de mo lai hien ngay (khong log) */
    s_code_polling = false;
    fd_zlink_code_t c;
    fd_zlink_code_get(&c);
    if (c.state == FD_ZLC_CODE && c.expires_s >= REUSE_MIN_S) return;
    set_code(FD_ZLC_IDLE, NULL, -1, NULL);
}

void fd_zlink_code_get(fd_zlink_code_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    *out = s_code;
    int64_t left = (s_code_deadline_us - esp_timer_get_time()) / 1000000;
    taskEXIT_CRITICAL(&s_mux);
    out->expires_s = out->state == FD_ZLC_CODE ? (left > 0 ? (int)left : 0) : 0;
}
