/* Lay doan Nhai. Khong log token hay body. */
#include "fd_shadow.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_shadow";
#define RESP_CAP (12 * 1024)

typedef struct {
    int gen;
    char length[8];
    int nids;
    char ids[FD_SYNC_MAX_COLS][FD_SYNC_WID_LEN];
} job_t;

static SemaphoreHandle_t s_mx;
static fd_shadow_status_t s_st;
static int s_gen;

static void ucpy(char *out, size_t cap, const char *in)
{
    size_t n = in ? strlen(in) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)in[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(out, in, n);
    out[n] = 0;
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

static void store_err(int gen, int http, bool empty, const char *msg)
{
    if (xSemaphoreTake(s_mx, portMAX_DELAY) != pdTRUE) return;
    if (gen == s_gen) {
        s_st.state = FD_SHADOW_ERR;
        s_st.http = http;
        s_st.empty = empty;
        s_st.n = 0;
        ucpy(s_st.message, sizeof(s_st.message), msg);
    }
    xSemaphoreGive(s_mx);
}

static void task(void *arg)
{
    job_t *job = arg;
    char id[16];
    fd_ota_get_device_id(id, sizeof(id));
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "device_id", id);
    cJSON_AddStringToObject(b, "length", job->length);
    cJSON *arr = cJSON_AddArrayToObject(b, "collections");
    for (int i = 0; arr && i < job->nids; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(job->ids[i]));
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    char *rx = heap_caps_malloc(RESP_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rx) rx = malloc(RESP_CAP);
    int code = (body && rx) ? fd_ota_api_post("/api/study/shadow/start", body, rx, RESP_CAP) : -1;
    free(body);
    cJSON *r = (code > 0 && rx) ? cJSON_Parse(rx) : NULL;
    const char *msg = r ? jstr(r, "message") : NULL;
    bool empty = r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "empty"));
    if (!r || code != 200 || empty) {
        store_err(job->gen, code, empty, msg);
        ESP_LOGI(TAG, "start -> %d empty=%d", code, empty ? 1 : 0);
    } else {
        fd_shadow_line_t tmp[FD_SHADOW_LINES];
        memset(tmp, 0, sizeof(tmp));
        int n = 0;
        const cJSON *lines = cJSON_GetObjectItemCaseSensitive(r, "lines");
        const cJSON *row;
        if (cJSON_IsArray(lines)) {
            cJSON_ArrayForEach(row, lines) {
                if (n >= FD_SHADOW_LINES) break;
                const char *text = jstr(row, "text");
                const char *vi = jstr(row, "vi");
                if (!text || !text[0] || !vi || !vi[0]) continue;
                ucpy(tmp[n].text, sizeof(tmp[n].text), text);
                ucpy(tmp[n].vi, sizeof(tmp[n].vi), vi);
                ucpy(tmp[n].ipa, sizeof(tmp[n].ipa), jstr(row, "ipa"));
                n++;
            }
        }
        if (xSemaphoreTake(s_mx, portMAX_DELAY) == pdTRUE) {
            if (job->gen == s_gen) {
                if (n > 0) {
                    memset(s_st.line, 0, sizeof(s_st.line));
                    memcpy(s_st.line, tmp, sizeof(tmp));
                    s_st.n = n;
                    s_st.http = code;
                    s_st.empty = false;
                    s_st.message[0] = 0;
                    s_st.state = FD_SHADOW_OK;
                } else {
                    s_st.state = FD_SHADOW_ERR;
                    s_st.http = code;
                    s_st.empty = false;
                    s_st.n = 0;
                    ucpy(s_st.message, sizeof(s_st.message), msg);
                }
            }
            xSemaphoreGive(s_mx);
        }
        ESP_LOGI(TAG, "start -> %d cau %d", code, n);
    }
    cJSON_Delete(r);
    free(rx);
    free(job);
    vTaskDelete(NULL);
}

bool fd_shadow_start(const char *length, const char ids[][FD_SYNC_WID_LEN], int nids)
{
    if (!length || !length[0] || !fd_net_is_connected()) return false;
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return false;
    job_t *job = calloc(1, sizeof(*job));
    if (!job) return false;
    ucpy(job->length, sizeof(job->length), length);
    if (ids && nids > 0) {
        for (int i = 0; i < nids && job->nids < FD_SYNC_MAX_COLS; i++) {
            if (!ids[i][0]) continue;
            ucpy(job->ids[job->nids], FD_SYNC_WID_LEN, ids[i]);
            job->nids++;
        }
    }
    if (xSemaphoreTake(s_mx, portMAX_DELAY) != pdTRUE) { free(job); return false; }
    s_gen++;
    job->gen = s_gen;
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = FD_SHADOW_LOADING;
    xSemaphoreGive(s_mx);
    if (xTaskCreate(task, "fd_shadow", 12288, job, 3, NULL) != pdPASS) {
        store_err(job->gen, -1, false, NULL);
        free(job);
        return false;
    }
    return true;
}

void fd_shadow_get(fd_shadow_status_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(20)) != pdTRUE) return;
    *out = s_st;
    xSemaphoreGive(s_mx);
}

void fd_shadow_cancel(void)
{
    if (!s_mx) return;
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(20)) != pdTRUE) return;
    s_gen++;
    if (s_st.state == FD_SHADOW_LOADING) s_st.state = FD_SHADOW_IDLE;
    xSemaphoreGive(s_mx);
}
