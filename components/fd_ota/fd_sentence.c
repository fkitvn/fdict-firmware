/* Sap xep thanh cau tu API: xem fd_sentence.h. KHONG log device_token / Authorization / body. */
#include "fd_sentence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_sen";

#define RESP_CAP (16 * 1024)   /* 10 cau x ~700 B + du phong */

typedef struct {
    bool done;
    int n;
    int8_t ord[FD_SEN_TOK];
    uint32_t ms;
} res_t;

static SemaphoreHandle_t s_mx;
static fd_sen_ex_t *s_ex;   /* FD_SEN_MAX, PSRAM */
static res_t s_res[FD_SEN_MAX];
static char s_sid[FD_SEN_ID_LEN];
static fd_sen_status_t s_st;
static int s_count = 10, s_streak;
static uint32_t s_dur;

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
    if (!s_ex) s_ex = ps_alloc(FD_SEN_MAX * sizeof(fd_sen_ex_t));
    return s_mx && s_ex;
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

/* mang chuoi -> the; false neu sai/qua dai */
static bool tok_list(const cJSON *a, char out[][FD_SEN_TOK_LEN], int *n)
{
    *n = 0;
    if (!cJSON_IsArray(a)) return false;
    const cJSON *t;
    cJSON_ArrayForEach(t, a) {
        if (!cJSON_IsString(t) || !t->valuestring || !t->valuestring[0]) return false;
        if (*n >= FD_SEN_TOK || strlen(t->valuestring) >= FD_SEN_TOK_LEN) return false;
        strcpy(out[(*n)++], t->valuestring);
    }
    return *n >= 2;
}

fd_study_err_t fd_sen_parse(const char *body, char *sid, int sid_cap, fd_sen_ex_t *out, int cap, int *n)
{
    *n = 0;
    cJSON *r = cJSON_Parse(body);
    if (!r) return FD_STUDY_ERR_FORMAT;
    const char *s = jstr(r, "session_id");
    const cJSON *xs = cJSON_GetObjectItemCaseSensitive(r, "exercises");
    if (s && s[0] && (int)strlen(s) < sid_cap && cJSON_IsArray(xs)) {
        strcpy(sid, s);
        const cJSON *x;
        cJSON_ArrayForEach(x, xs) {
            if (*n >= cap) break;
            fd_sen_ex_t *d = &out[*n];
            memset(d, 0, sizeof(*d));
            const char *id = jstr(x, "id");
            int na = 0;
            if (!id || !id[0] || strlen(id) >= sizeof(d->id)) continue;
            if (!tok_list(cJSON_GetObjectItemCaseSensitive(x, "tokens"), d->tok, &d->n)) continue;
            if (!tok_list(cJSON_GetObjectItemCaseSensitive(x, "answer"), d->ans, &na) || na != d->n) continue;
            strcpy(d->id, id);
            ucpy(d->end, sizeof(d->end), jstr(x, "end"));
            ucpy(d->sentence, sizeof(d->sentence), jstr(x, "sentence"));
            ucpy(d->hint, sizeof(d->hint), jstr(x, "hint"));
            (*n)++;
        }
    }
    cJSON_Delete(r);
    return *n > 0 ? FD_STUDY_ERR_NONE : FD_STUDY_ERR_FORMAT;
}

static fd_study_err_t http_err(int code)
{
    if (code == 401) return FD_STUDY_ERR_UNPAIRED;   /* fd_ota da xoa token + ten */
    if (code < 0) return FD_STUDY_ERR_NET;
    return FD_STUDY_ERR_SERVER;                      /* 400/403/404/409/416...: chi bao loi (message do ui_nav hien) */
}

static char *build_start(void)
{
    char id[16];
    fd_ota_get_device_id(id, sizeof(id));
    cJSON *b = cJSON_CreateObject();
    if (!b) return NULL;
    cJSON_AddStringToObject(b, "device_id", id);
    cJSON_AddNumberToObject(b, "count", s_count);
    char *s = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    return s;
}

/* goi khi dang giu lock */
static char *build_result(void)
{
    char id[16];
    fd_ota_get_device_id(id, sizeof(id));
    cJSON *b = cJSON_CreateObject();
    if (!b) return NULL;
    cJSON_AddStringToObject(b, "device_id", id);
    cJSON_AddStringToObject(b, "session_id", s_sid);
    cJSON *rs = cJSON_AddArrayToObject(b, "results");
    for (int i = 0; rs && i < s_st.n; i++) {
        if (!s_res[i].done) continue;
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "id", s_ex[i].id);
        cJSON *t = cJSON_AddArrayToObject(o, "tokens");
        for (int k = 0; t && k < s_res[i].n; k++) cJSON_AddItemToArray(t, cJSON_CreateString(s_ex[i].tok[s_res[i].ord[k]]));
        cJSON_AddNumberToObject(o, "ms", s_res[i].ms);
        cJSON_AddItemToArray(rs, o);
    }
    cJSON_AddNumberToObject(b, "best_streak", s_streak);
    cJSON_AddNumberToObject(b, "duration_ms", s_dur);
    char *s = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    return s;
}

static void task(void *arg)
{
    bool submit = arg != NULL;
    char *rx = ps_alloc(RESP_CAP), *body = NULL;
    fd_sen_ex_t *tmp = submit ? NULL : ps_alloc(FD_SEN_MAX * sizeof(fd_sen_ex_t));
    char sid[FD_SEN_ID_LEN] = "";
    fd_study_err_t err = FD_STUDY_ERR_NOMEM;
    int code = 0, n = 0;
    if (rx && (submit || tmp)) {
        if (!fd_ota_is_paired()) err = FD_STUDY_ERR_UNPAIRED;
        else if (!fd_net_is_connected()) err = FD_STUDY_ERR_NO_WIFI;
        else {
            if (submit) { lock(); body = build_result(); unlock(); }
            else body = build_start();
            if (body) {
                code = fd_ota_api_post_ex(submit ? "/api/study/sentence/result" : "/api/study/sentence/start", body, rx,
                                          RESP_CAP, true);   /* 429: cho Retry-After roi thu lai */
                if (submit) err = (code == 200 || code == 409) ? FD_STUDY_ERR_NONE : http_err(code);   /* 409 used = da gui */
                else err = code == 200 ? fd_sen_parse(rx, sid, sizeof(sid), tmp, FD_SEN_MAX, &n) : http_err(code);
            }
        }
    }
    ESP_LOGI(TAG, "%s: err=%d http=%d n=%d", submit ? "result" : "start", err, code, n);
    lock();
    if (!err && submit) s_st.submitted = true;
    if (!err && !submit) {
        memcpy(s_ex, tmp, n * sizeof(*tmp));
        memset(s_res, 0, sizeof(s_res));
        strcpy(s_sid, sid);
        s_st.n = n;
        s_st.submitted = false;
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

static bool spawn(bool submit)
{
    lock();
    bool busy = s_st.busy;
    if (!busy) { s_st.busy = true; s_st.err = FD_STUDY_ERR_NONE; s_st.http = 0; }
    unlock();
    if (busy) return false;
    if (xTaskCreate(task, "fd_sen", 6144, submit ? (void *)1 : NULL, 4, NULL) != pdPASS) {
        lock(); s_st.busy = false; s_st.err = FD_STUDY_ERR_NOMEM; unlock();
        return false;
    }
    return true;
}

bool fd_sen_start(int count)
{
    if (!init()) return false;
    lock();
    bool busy = s_st.busy;
    if (!busy) { s_count = count == 5 ? 5 : 10; s_st.n = 0; s_st.submitted = false; s_sid[0] = 0; }
    unlock();
    return !busy && spawn(false);
}

bool fd_sen_submit(int best_streak, uint32_t duration_ms)
{
    if (!init()) return false;
    lock();
    bool ok = s_sid[0] && !s_st.submitted && !s_st.busy;
    s_streak = best_streak;
    s_dur = duration_ms;
    unlock();
    return ok && spawn(true);
}

void fd_sen_get(fd_sen_status_t *out)
{
    if (!out) return;
    if (!init()) { memset(out, 0, sizeof(*out)); return; }
    lock(); *out = s_st; unlock();
}

bool fd_sen_ex(int i, fd_sen_ex_t *out)
{
    if (!init() || !out) return false;
    lock();
    bool ok = i >= 0 && i < s_st.n && !s_st.busy;
    if (ok) *out = s_ex[i];
    unlock();
    return ok;
}

bool fd_sen_answer(int i, const int *order, int n, uint32_t ms)
{
    if (!init() || !order) return false;
    bool ok = false;
    lock();
    if (i >= 0 && i < s_st.n && n == s_ex[i].n) {
        res_t *r = &s_res[i];
        r->done = true;
        r->n = n;
        r->ms = ms;
        ok = true;
        for (int k = 0; k < n; k++) {
            int o = order[k];
            if (o < 0 || o >= n) { ok = false; r->ord[k] = 0; continue; }
            r->ord[k] = (int8_t)o;
            if (strcasecmp(s_ex[i].tok[o], s_ex[i].ans[k]) != 0) ok = false;   /* tu lap (the, a) van dung */
        }
    }
    unlock();
    return ok;
}
