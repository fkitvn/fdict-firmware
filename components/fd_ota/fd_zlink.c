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
#define POLL_MS 4000
#define RESP_CAP 384

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static volatile int s_linked = FD_ZL_UNKNOWN;
static volatile uint32_t s_gen, s_srv_gen;
static fd_zlink_code_t s_code;
static int64_t s_code_deadline_us;
static volatile uint32_t s_code_seq;   /* tang khi start/stop -> vong poll cu tu dung */

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
    if (!fd_ota_is_paired()) { set_linked(FD_ZL_UNKNOWN); return; }
    if (!wait_wifi(90)) return;
    char *resp = malloc(RESP_CAP);
    if (!resp) return;
    int code = call("/api/devices/zalo-link/status", resp);
    if (code == 200) set_linked(json_true(resp, "zalo_linked") ? FD_ZL_YES : FD_ZL_NO);
    else if (code == 401) set_linked(FD_ZL_UNKNOWN);
    ESP_LOGI(TAG, "status -> %d", code);
    free(resp);
}

static void do_code(uint32_t seq)
{
    char *resp = malloc(RESP_CAP);
    if (!resp) { set_code(FD_ZLC_ERROR, NULL, -1, "nomem"); return; }
    set_code(FD_ZLC_REQUESTING, NULL, -1, NULL);
    if (!fd_net_is_connected()) { set_code(FD_ZLC_ERROR, NULL, -1, "no_wifi"); goto out; }
    int code = call("/api/devices/zalo-link/start", resp);
    ESP_LOGI(TAG, "start -> %d", code);   /* khong log body (co ma) */
    if (seq != s_code_seq) goto out;
    if (code == 200 && json_true(resp, "linked")) { set_linked(FD_ZL_YES); set_code(FD_ZLC_DONE, NULL, -1, NULL); goto out; }
    char c[8];
    if (code != 200 || !json_code(resp, c)) {
        char e[20];
        snprintf(e, sizeof(e), code < 0 ? "net" : "http_%d", code);
        set_code(FD_ZLC_ERROR, NULL, -1, e);
        goto out;
    }
    set_code(FD_ZLC_CODE, c, json_int(resp, "expires_in", 300), NULL);
    memset(resp, 0, RESP_CAP);
    memset(c, 0, sizeof(c));
    while (seq == s_code_seq) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        if (seq != s_code_seq) break;
        if (esp_timer_get_time() >= s_code_deadline_us) { set_code(FD_ZLC_EXPIRED, NULL, -1, NULL); break; }
        if (!fd_net_is_connected()) continue;
        int pc = call("/api/devices/zalo-link/poll", resp);
        if (seq != s_code_seq) break;
        if (pc == 200 && json_true(resp, "linked")) {
            set_linked(FD_ZL_YES);
            set_code(FD_ZLC_DONE, NULL, -1, NULL);
            break;
        }
        if (pc == 410 || pc == 404) { set_code(FD_ZLC_EXPIRED, NULL, -1, NULL); break; }
        if (pc == 401) { set_linked(FD_ZL_UNKNOWN); set_code(FD_ZLC_ERROR, NULL, -1, "unpaired"); break; }
    }
out:
    free(resp);
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (bits & BIT_START) do_code(s_code_seq);
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
    if (s_srv_gen != fd_ota_server_gen()) { s_srv_gen = fd_ota_server_gen(); set_linked(FD_ZL_UNKNOWN); }
    if (!fd_ota_is_paired()) { set_linked(FD_ZL_UNKNOWN); return; }
    kick(BIT_STATUS);
}

fd_zlink_linked_t fd_zlink_linked(void)
{
    if (!fd_ota_is_paired() || s_srv_gen != fd_ota_server_gen()) return FD_ZL_UNKNOWN;
    return (fd_zlink_linked_t)s_linked;
}

uint32_t fd_zlink_gen(void) { return s_gen; }

void fd_zlink_code_start(void)
{
    s_code_seq++;
    set_code(FD_ZLC_REQUESTING, NULL, -1, NULL);
    kick(BIT_START);
}

void fd_zlink_code_stop(void)
{
    s_code_seq++;
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
