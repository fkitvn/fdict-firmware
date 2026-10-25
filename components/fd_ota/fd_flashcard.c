/* Flash card tu API: xem fd_flashcard.h. KHONG log device_token / Authorization / body. */
#include "fd_flashcard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_fc";

#define RESP_CAP   (32 * 1024)   /* 100 the x ~250 B + du phong */
#define CACHE      "/data/fc_cache.json"
#define CACHE_TMP  "/data/fc_cache.tmp"

static SemaphoreHandle_t s_mx;
static fd_fc_card_t *s_cards;   /* FD_FC_MAX, PSRAM */
static fd_fc_status_t s_st;
static bool s_cache_tried;

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

static bool init(void)
{
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_cards) s_cards = ps_alloc(FD_FC_MAX * sizeof(fd_fc_card_t));
    return s_mx && s_cards;
}

/* sao chep chuoi UTF-8 an toan (nhu fd_study) */
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

fd_study_err_t fd_fc_parse(const char *body, fd_fc_card_t *out, int cap, int *n)
{
    *n = 0;
    cJSON *r = cJSON_Parse(body);
    if (!r) return FD_STUDY_ERR_FORMAT;
    fd_study_err_t err = FD_STUDY_ERR_FORMAT;
    const cJSON *cs = cJSON_GetObjectItemCaseSensitive(r, "cards");
    if (cJSON_IsArray(cs)) {
        const cJSON *c;
        cJSON_ArrayForEach(c, cs) {
            if (*n >= cap) break;
            const char *wd = jstr(c, "word"), *mn = jstr(c, "meaning");
            if (!wd || !wd[0] || !mn || !mn[0]) continue;   /* bo the thieu truong */
            fd_fc_card_t *d = &out[*n];
            ucpy(d->word, sizeof(d->word), wd);
            ucpy(d->ipa, sizeof(d->ipa), jstr(c, "ipa"));
            ucpy(d->meaning, sizeof(d->meaning), mn);
            (*n)++;
        }
        if (*n > 0) err = FD_STUDY_ERR_NONE;
    }
    cJSON_Delete(r);
    return err;
}

static fd_study_err_t http_err(int code)
{
    if (code == 401) return FD_STUDY_ERR_UNPAIRED;   /* fd_ota da xoa token + ten */
    if (code < 0) return FD_STUDY_ERR_NET;
    return FD_STUDY_ERR_SERVER;                      /* 400/403/404/409/416...: chi bao loi, giu the cu */
}

static bool write_atomic(const char *data)
{
    FILE *f = fopen(CACHE_TMP, "w");
    bool ok = f && fputs(data, f) >= 0;
    if (f) {
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;
        ok = (fclose(f) == 0) && ok;
    }
    ok = ok && rename(CACHE_TMP, CACHE) == 0;
    if (!ok) unlink(CACHE_TMP);
    return ok;
}

/* doc cache vao s_cards (goi tu task) */
static void load_cache(fd_fc_card_t *tmp, char *rx)
{
    FILE *f = fopen(CACHE, "r");
    if (!f) return;
    size_t len = fread(rx, 1, RESP_CAP - 1, f);
    fclose(f);
    rx[len] = 0;
    int n;
    if (fd_fc_parse(rx, tmp, FD_FC_MAX, &n) != FD_STUDY_ERR_NONE) return;
    lock();
    if (s_st.n == 0) {   /* chua co the online */
        memcpy(s_cards, tmp, n * sizeof(*tmp));
        s_st.n = n;
    }
    unlock();
    ESP_LOGI(TAG, "cache: %d the", n);
}

static void task(void *arg)
{
    (void)arg;
    fd_fc_card_t *tmp = ps_alloc(FD_FC_MAX * sizeof(fd_fc_card_t));
    char *rx = ps_alloc(RESP_CAP);
    char *body = NULL;
    fd_study_err_t err = FD_STUDY_ERR_NOMEM;
    int code = 0, n = 0;
    if (tmp && rx) {
        if (!s_cache_tried) { s_cache_tried = true; load_cache(tmp, rx); }
        if (!fd_ota_is_paired()) err = FD_STUDY_ERR_UNPAIRED;
        else if (!fd_net_is_connected()) err = FD_STUDY_ERR_NO_WIFI;
        else {
            char id[16];
            fd_ota_get_device_id(id, sizeof(id));
            cJSON *b = cJSON_CreateObject();
            if (b) {
                cJSON_AddStringToObject(b, "device_id", id);
                cJSON_AddNumberToObject(b, "offset", 0);
                cJSON_AddNumberToObject(b, "limit", FD_FC_MAX);
                body = cJSON_PrintUnformatted(b);
                cJSON_Delete(b);
            }
            if (body) {
                code = fd_ota_api_post_ex("/api/study/cards", body, rx, RESP_CAP, true);   /* nen: cho Retry-After khi 429 */
                err = code == 200 ? fd_fc_parse(rx, tmp, FD_FC_MAX, &n) : http_err(code);
                if (!err && !write_atomic(rx)) ESP_LOGW(TAG, "khong ghi duoc cache");
            }
        }
    }
    ESP_LOGI(TAG, "tai the: err=%d http=%d n=%d", err, code, n);
    lock();
    if (!err) {
        memcpy(s_cards, tmp, n * sizeof(*tmp));
        s_st.n = n;
        s_st.online = true;
    }
    s_st.err = err;
    s_st.http = code;
    s_st.busy = false;
    unlock();
    free(body);
    free(rx);
    free(tmp);
    vTaskDelete(NULL);
}

bool fd_fc_refresh(void)
{
    if (!init()) return false;
    lock();
    bool busy = s_st.busy;
    if (!busy) s_st.busy = true;
    unlock();
    if (busy) return false;
    if (xTaskCreate(task, "fd_fc", 6144, NULL, 4, NULL) != pdPASS) {
        lock(); s_st.busy = false; s_st.err = FD_STUDY_ERR_NOMEM; unlock();
        return false;
    }
    return true;
}

void fd_fc_get(fd_fc_status_t *out)
{
    if (!init()) { memset(out, 0, sizeof(*out)); return; }
    lock(); *out = s_st; unlock();
}

int fd_fc_copy(fd_fc_card_t *out, int cap)
{
    if (!init() || !out || cap <= 0) return 0;
    lock();
    int n = s_st.n < cap ? s_st.n : cap;
    memcpy(out, s_cards, n * sizeof(*out));
    unlock();
    return n;
}
