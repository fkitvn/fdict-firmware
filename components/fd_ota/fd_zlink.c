/* Warranty status only. No activation mutation, token logging, or background poll. */
#include "fd_zlink.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fd_zlink";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static fd_warranty_status_t s_status;
static bool s_busy;

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


static bool expiry_date(const char *iso, char *out, size_t n)
{
    int y, mo, d, h;
    if (!iso || sscanf(iso, "%4d-%2d-%2dT%2d", &y, &mo, &d, &h) != 4 ||
        y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23) return false;
    long z = days_from_civil(y, mo, d) + (h + 7 >= 24 ? 1 : 0);
    civil_from_days(z, &y, &mo, &d);
    if (y < 1970 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    snprintf(out, n, "%02u/%02u/%04u", (unsigned)d, (unsigned)mo, (unsigned)y);
    return true;
}

static void complete(const fd_warranty_status_t *st)
{
    taskENTER_CRITICAL(&s_mux);
    s_status = *st;
    s_busy = false;
    taskEXIT_CRITICAL(&s_mux);
}

static void task(void *arg)
{
    uint32_t server = (uint32_t)(uintptr_t)arg;
    fd_warranty_status_t st = {.state = FD_WARR_ERROR};
    char id[16], body[48], resp[384] = {0};
    fd_ota_get_device_id(id, sizeof(id));
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", id);
    int code = fd_ota_api_post_ex("/api/devices/warranty/lookup", body, resp, sizeof(resp), false);
    ESP_LOGI(TAG, "warranty lookup HTTP %d", code);
    if (server != fd_ota_server_gen()) {
        strlcpy(st.err, "server_changed", sizeof(st.err));
    } else if (code != 200) {
        if (code < 0) strlcpy(st.err, "network", sizeof(st.err));
        else snprintf(st.err, sizeof(st.err), "HTTP %d", code);
    } else {
        cJSON *j = cJSON_Parse(resp);
        const cJSON *activated = cJSON_GetObjectItemCaseSensitive(j, "warranty_activated");
        const cJSON *expires = cJSON_GetObjectItemCaseSensitive(j, "warranty_expires_at");
        st.activated = cJSON_IsTrue(activated);
        st.active = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "active"));
        if (cJSON_IsBool(activated) && (!st.activated ||
            (cJSON_IsString(expires) && expiry_date(expires->valuestring, st.expires, sizeof(st.expires))))) {
            st.state = FD_WARR_READY;
        } else strlcpy(st.err, "format", sizeof(st.err));
        cJSON_Delete(j);
    }
    complete(&st);
    vTaskDelete(NULL); /* return 8 KB internal stack instead of keeping an idle worker */
}

void fd_zlink_refresh(void)
{
    taskENTER_CRITICAL(&s_mux);
    if (s_busy) { taskEXIT_CRITICAL(&s_mux); return; }
    s_busy = true;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = FD_WARR_LOADING;
    taskEXIT_CRITICAL(&s_mux);
    fd_warranty_status_t err = {.state = FD_WARR_ERROR};
    if (!fd_net_is_connected()) {
        strlcpy(err.err, "no_wifi", sizeof(err.err));
        complete(&err);
    } else if (xTaskCreate(task, "fd_warranty", 8192, (void *)(uintptr_t)fd_ota_server_gen(), 3, NULL) != pdPASS) {
        strlcpy(err.err, "nomem", sizeof(err.err));
        complete(&err);
    }
}

void fd_zlink_status_get(fd_warranty_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mux);
    *out = s_status;
    taskEXIT_CRITICAL(&s_mux);
}

/* Independent Zalo linking: no credentials or device ID in the QR URL. */
#include "esp_timer.h"
static fd_link_status_t s_link;
static bool s_link_active, s_link_busy, s_link_checked, s_unlink_requested;
static uint32_t s_link_gen, s_link_server;
static int64_t s_link_expiry, s_link_next;
typedef enum { LINK_STATUS, LINK_START, LINK_POLL, LINK_UNLINK } link_op_t;

static void link_task(void *arg)
{
    uint32_t generation = (uint32_t)(uintptr_t)arg;
    taskENTER_CRITICAL(&s_mux);
    if (generation != s_link_gen || !s_link_active) {
        s_link_busy = false;
        taskEXIT_CRITICAL(&s_mux);
        vTaskDelete(NULL);
        return;
    }
    link_op_t op = s_unlink_requested ? LINK_UNLINK :
        (!s_link_checked || s_link.state == FD_LINK_DONE) ? LINK_STATUS :
        !s_link.code[0] ? LINK_START : LINK_POLL;
    s_unlink_requested = false;
    uint32_t server = s_link_server;
    taskEXIT_CRITICAL(&s_mux);
    char id[16], body[72], response[384] = {0};
    fd_ota_get_device_id(id, sizeof(id));
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"confirm\":true}", id);
    const char *path = op == LINK_STATUS ? "/api/devices/zalo-link/status" :
        op == LINK_UNLINK ? "/api/devices/zalo-link/unlink" :
        op == LINK_START ? "/api/devices/zalo-link/start" : "/api/devices/zalo-link/poll";
    int64_t began = esp_timer_get_time();
    int http = fd_ota_api_post_ex(path, body, response, sizeof(response), false);
    fd_link_status_t result = {.state = FD_LINK_ERROR};
    int seconds = 0;
    cJSON *j = http == 200 ? cJSON_Parse(response) : NULL;
    if (op == LINK_UNLINK) {
        if (j && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "ok")) &&
            cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(j, "zalo_linked"))) {
            result.state = FD_LINK_LOADING;
        } else {
            result.state = FD_LINK_DONE;
            strlcpy(result.err, "unlink_failed", sizeof(result.err));
        }
    } else if (op == LINK_STATUS) {
        const cJSON *linked = cJSON_GetObjectItemCaseSensitive(j, "zalo_linked");
        if (cJSON_IsBool(linked)) result.state = cJSON_IsTrue(linked) ? FD_LINK_DONE : FD_LINK_LOADING;
        else strlcpy(result.err, "status_failed", sizeof(result.err));
    } else if (j && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "linked"))) result.state = FD_LINK_DONE;
    else if (j) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(j, "code");
        const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
        bool valid_code = op != LINK_START || (cJSON_IsString(code) && strlen(code->valuestring) == 6 &&
            strspn(code->valuestring, "0123456789") == 6);
        if (valid_code && cJSON_IsNumber(ttl) && ttl->valueint > 0 && ttl->valueint <= 300) {
            result.state = FD_LINK_CODE;
            if (op == LINK_START) strlcpy(result.code, code->valuestring, sizeof(result.code));
            seconds = ttl->valueint;
        } else strlcpy(result.err, "format", sizeof(result.err));
    } else if (http == 410 || http == 404) result.state = FD_LINK_LOADING;
    else if (http < 0) strlcpy(result.err, "network", sizeof(result.err));
    else snprintf(result.err, sizeof(result.err), "HTTP %d", http);
    cJSON_Delete(j);
    bool still_paired = fd_ota_is_paired();
    taskENTER_CRITICAL(&s_mux);
    if (s_link_active && generation == s_link_gen && server == fd_ota_server_gen() && still_paired) {
        if (op == LINK_STATUS && result.state != FD_LINK_ERROR) s_link_checked = true;
        if (result.state == FD_LINK_CODE) {
            if (op == LINK_START) strlcpy(s_link.code, result.code, sizeof(s_link.code));
            s_link.state = FD_LINK_CODE;
            s_link_expiry = began + (int64_t)seconds * 1000000;
        } else if (op == LINK_POLL && result.state == FD_LINK_ERROR && http < 0) {
            /* A transient network failure does not discard a still-valid code. */
        } else if (op == LINK_STATUS && s_link.state == FD_LINK_DONE && result.state == FD_LINK_ERROR && http < 0) {
            /* Keep the last confirmed link visible while offline. */
        } else {
            s_link = result;
            s_link_expiry = 0;
        }
        s_link_next = result.state == FD_LINK_LOADING ? 0 : esp_timer_get_time() + 5000000;
    }
    s_link_busy = false;
    taskEXIT_CRITICAL(&s_mux);
    vTaskDelete(NULL);
}

void fd_zalo_link_stop(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_link_active = false;
    s_unlink_requested = false;
    ++s_link_gen;
    memset(&s_link, 0, sizeof(s_link));
    taskEXIT_CRITICAL(&s_mux);
}
void fd_zalo_link_start(void)
{
    taskENTER_CRITICAL(&s_mux);
    ++s_link_gen;
    s_link_server = fd_ota_server_gen();
    s_link_active = true;
    s_link_checked = s_unlink_requested = false;
    s_link_expiry = s_link_next = 0;
    memset(&s_link, 0, sizeof(s_link));
    s_link.state = FD_LINK_LOADING;
    taskEXIT_CRITICAL(&s_mux);
    fd_zalo_link_tick();
}
void fd_zalo_link_unlink(void)
{
    bool paired = fd_ota_is_paired();
    taskENTER_CRITICAL(&s_mux);
    if (!s_link_active || s_link.state != FD_LINK_DONE || !paired || s_link_server != fd_ota_server_gen()) {
        taskEXIT_CRITICAL(&s_mux); return;
    }
    /* If a status fetch is in flight, generation rejects its result; tick starts unlink after it exits. */
    ++s_link_gen;
    s_unlink_requested = true;
    s_link.state = FD_LINK_UNLINKING;
    s_link.err[0] = 0;
    s_link_next = 0;
    taskEXIT_CRITICAL(&s_mux);
    fd_zalo_link_tick();
}
void fd_zalo_link_tick(void)
{
    bool paired = fd_ota_is_paired();
    bool connected = fd_net_is_connected();
    int64_t t = esp_timer_get_time();
    taskENTER_CRITICAL(&s_mux);
    if (!s_link_active || s_link_busy) { taskEXIT_CRITICAL(&s_mux); return; }
    if (!paired || s_link_server != fd_ota_server_gen()) {
        s_link.state = FD_LINK_ERROR;
        s_link.code[0] = 0;
        strlcpy(s_link.err, "login_required", sizeof(s_link.err));
        s_link_active = false;
        taskEXIT_CRITICAL(&s_mux); return;
    }
    if (s_link_expiry && t >= s_link_expiry) {
        s_link.state = FD_LINK_LOADING;
        s_link.code[0] = 0;
        s_link_expiry = s_link_next = 0;
    }
    if (t < s_link_next || s_link.state == FD_LINK_ERROR) { taskEXIT_CRITICAL(&s_mux); return; }
    if (!connected) { s_link_next = t + 5000000; taskEXIT_CRITICAL(&s_mux); return; }
    s_link_busy = true;
    uint32_t gen = s_link_gen;
    taskEXIT_CRITICAL(&s_mux);
    if (xTaskCreate(link_task, "fd_zalo_link", 8192, (void *)(uintptr_t)gen, 3, NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_mux);
        s_link_busy = false;
        s_link.state = FD_LINK_ERROR;
        strlcpy(s_link.err, "nomem", sizeof(s_link.err));
        taskEXIT_CRITICAL(&s_mux);
    }
}
void fd_zalo_link_get(fd_link_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mux);
    *out = s_link;
    int64_t remaining = s_link_expiry - esp_timer_get_time();
    out->seconds = remaining > 0 ? (int)((remaining + 999999) / 1000000) : 0;
    taskEXIT_CRITICAL(&s_mux);
}
