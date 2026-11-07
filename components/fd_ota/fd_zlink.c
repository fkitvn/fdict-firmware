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
