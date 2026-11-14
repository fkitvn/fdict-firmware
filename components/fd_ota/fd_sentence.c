/* Sap xep thanh cau tu API: xem fd_sentence.h. KHONG log device_token / Authorization / body. */
#include "fd_sentence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_sen";

#define RESP_CAP (32 * 1024)   /* 40 cau x ~700 B + du phong */

typedef struct {
    bool done;
    int n;
    int8_t ord[FD_SEN_TOK];
    uint32_t ms;
} res_t;

#define SDIR      "/data/sen"
#define CACHE     SDIR "/cache.json"
#define CACHE_TMP SDIR "/cache.tmp"
#define USED      SDIR "/cache.usd"   /* phien trong cache da lam (khong gui lai) */
#define SYNC_GAP_US (20 * 1000000LL)

static SemaphoreHandle_t s_mx;
static fd_sen_ex_t *s_ex;   /* FD_SEN_MAX, PSRAM */
static res_t s_res[FD_SEN_MAX];
static char s_sid[FD_SEN_ID_LEN];
static fd_sen_status_t s_st;
static int s_count = 10, s_streak;
static uint32_t s_dur;
static bool s_local;                /* 2.3.0: bo cau tu kho dong bo: moi cau tra loi = su kien fd_sync */
static char s_wid[FD_SEN_MAX][FD_SYNC_WID_LEN];
static bool s_can_submit;           /* bo cau hien tai la phien server chua lam */
static volatile bool s_sync_busy;
static int64_t s_sync_last;

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


static int rnd(int n) { return n > 1 ? (int)(esp_random() % (uint32_t)n) : 0; }

/* xao the cua cau (khac thu tu dung neu co the) */
static void shuffle_ex(fd_sen_ex_t *e)
{
    for (int tries = 0; tries < 6; tries++) {
        memcpy(e->tok, e->ans, sizeof(e->tok));
        for (int i = e->n - 1; i > 0; i--) {
            int j = rnd(i + 1);
            char t[FD_SEN_TOK_LEN];
            memcpy(t, e->tok[i], sizeof(t));
            memcpy(e->tok[i], e->tok[j], sizeof(t));
            memcpy(e->tok[j], t, sizeof(t));
        }
        bool same = true;
        for (int i = 0; i < e->n && same; i++) same = strcasecmp(e->tok[i], e->ans[i]) == 0;
        if (!same) return;
    }
    for (int i = 0; i < e->n; i++) memcpy(e->tok[i], e->ans[e->n - 1 - i], FD_SEN_TOK_LEN);   /* dao nguoc */
}


/* ---------- file ---------- */

static bool write_atomic(const char *path, const char *tmp, const char *data)
{
    mkdir(SDIR, 0775);
    FILE *f = fopen(tmp, "w");
    bool ok = f && fputs(data, f) >= 0;
    if (f) {
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;
        ok = (fclose(f) == 0) && ok;
    }
    if (ok) unlink(path);
    ok = ok && rename(tmp, path) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

static char *read_file(const char *path, char *rx, int cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t len = fread(rx, 1, cap - 1, f);
    fclose(f);
    rx[len] = 0;
    return len ? rx : NULL;
}

static int outbox_count(void)
{
    DIR *d = opendir(SDIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (!strncmp(e->d_name, "o_", 2) && strstr(e->d_name, ".json")) n++;
    closedir(d);
    return n;
}

/* ---------- body ---------- */

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
    (void)arg;
    fd_sen_ex_t *tmp = ps_alloc(FD_SEN_MAX * sizeof(fd_sen_ex_t));
    char sid[FD_SEN_ID_LEN] = "";
    fd_study_err_t err = FD_STUDY_ERR_NOMEM;
    int code = 0, n = 0, src = FD_SEN_SRC_ONLINE;
    bool submit = false, local = false;
    static char wid[FD_SEN_MAX][FD_SYNC_WID_LEN];
    if (tmp) {
        err = FD_STUDY_ERR_NONE;
        n = fd_sync_build_sentences(tmp, wid, s_count);   /* chi kho dong bo, khong goi API luc bat dau */
        if (n > 0) {
            local = true;
            src = FD_SEN_SRC_CACHE;
            for (int i = 0; i < n; i++) shuffle_ex(&tmp[i]);
        } else err = FD_STUDY_ERR_NO_DECK;
    }
    ESP_LOGI(TAG, "start: src=%d http=%d n=%d", src, code, n);
    lock();
    if (!err) {
        memcpy(s_ex, tmp, n * sizeof(*tmp));
        memset(s_res, 0, sizeof(s_res));
        strcpy(s_sid, sid);
        s_st.n = n;
        s_st.source = src;
        s_can_submit = submit;
        s_local = local;
        if (local) memcpy(s_wid, wid, sizeof(s_wid));
    } else s_st.n = 0;
    s_st.err = err;
    s_st.http = code;
    s_st.busy = false;
    unlock();
    free(tmp);
    vTaskDelete(NULL);
}

/* gui hang doi + nap san cache */
static void sync_task(void *arg)
{
    (void)arg;
    char *rx = ps_alloc(RESP_CAP), *body = ps_alloc(RESP_CAP);
    int sent = 0, kept = 0;
    if (rx && body && fd_ota_is_paired() && fd_net_is_connected()) {
        bool stop = false;
        while (!stop) {
            char path[sizeof(SDIR) + 1 + 256] = "";   /* d_name toi da 255 */
            lock();
            DIR *d = opendir(SDIR);
            struct dirent *e;
            while (d && (e = readdir(d)) != NULL)
                if (!strncmp(e->d_name, "o_", 2) && strstr(e->d_name, ".json") && strlen(e->d_name) < 40) {
                    snprintf(path, sizeof(path), SDIR "/%s", e->d_name);
                    break;
                }
            if (d) closedir(d);
            char *b = path[0] ? read_file(path, body, RESP_CAP) : NULL;
            unlock();
            if (!path[0]) break;
            if (!b) { unlink(path); continue; }
            int code = fd_ota_api_post_ex("/api/study/sentence/result", b, rx, RESP_CAP, true);
            if (code == 200 || code == 409 || code == 400 || code == 403 || code == 404) {   /* 409 used = da gui */
                lock(); unlink(path); unlock();
                sent++;
            } else { kept++; stop = true; }   /* mang / 401 / 429 / 5xx: giu, thu lai sau */
        }
    }
    int ob = outbox_count();
    if (sent || kept) ESP_LOGI(TAG, "sync: gui %d, con %d", sent, ob);
    lock();
    s_st.outbox = ob;
    unlock();
    free(rx);
    free(body);
    s_sync_busy = false;
    vTaskDelete(NULL);
}

static void sync_kick(bool force)
{
    int64_t now = esp_timer_get_time();
    if (s_sync_busy || (!force && s_sync_last && now - s_sync_last < SYNC_GAP_US)) return;
    s_sync_last = now;
    s_sync_busy = true;
    if (xTaskCreate(sync_task, "fd_sen_sync", 6144, NULL, 3, NULL) != pdPASS) s_sync_busy = false;
}

void fd_sen_sync(void)
{
    if (!init()) return;
    if (!fd_net_is_connected() || !fd_ota_is_paired()) {
        lock(); s_st.outbox = outbox_count(); unlock();
        return;
    }
    sync_kick(false);
}

bool fd_sen_start(int count)
{
    if (!init()) return false;
    lock();
    bool busy = s_st.busy;
    if (!busy) {
        s_count = (count == 0) ? FD_SEN_MAX : (count == 20 ? 20 : 10);
        s_st.n = 0; s_st.submitted = false; s_sid[0] = 0; s_can_submit = false;
        s_st.busy = true; s_st.err = FD_STUDY_ERR_NONE; s_st.http = 0;
    }
    unlock();
    if (busy) return false;
    if (xTaskCreate(task, "fd_sen", 6144, NULL, 4, NULL) != pdPASS) {
        lock(); s_st.busy = false; s_st.err = FD_STUDY_ERR_NOMEM; unlock();
        return false;
    }
    return true;
}

bool fd_sen_submit(int best_streak, uint32_t duration_ms)
{
    if (!init()) return false;
    lock();
    bool ok = !s_st.submitted && !s_st.busy && s_st.n > 0;
    bool sync_local = false;
    char *body = NULL;
    if (ok) {
        s_st.submitted = true;
        s_streak = best_streak;
        s_dur = duration_ms;
        if (s_can_submit && s_sid[0]) {
            body = build_result();
            char path[64];
            snprintf(path, sizeof(path), SDIR "/o_%08lx.json", (unsigned long)esp_random());
            char tp[64];
            snprintf(tp, sizeof(tp), SDIR "/o_tmp.tmp");
            if (!body || !write_atomic(path, tp, body)) ESP_LOGW(TAG, "khong ghi duoc hang doi ket qua");
        }
        if (s_local) sync_local = true;
        else if (s_st.source != FD_SEN_SRC_BUILTIN) {   /* phien cache/online da lam: khong gui lai */
            FILE *f = fopen(USED, "w");
            if (f) fclose(f);
        }
        s_st.outbox = outbox_count();
    }
    unlock();
    free(body);
    if (sync_local) fd_sync_kick();
    else if (ok) sync_kick(true);
    return ok;
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
    bool ok = false, first = false;
    char wid[FD_SYNC_WID_LEN] = "";
    char sentence[FD_SEN_TXT_LEN] = "";
    lock();
    if (i >= 0 && i < s_st.n && n == s_ex[i].n) {
        res_t *r = &s_res[i];
        first = !r->done && s_local;
        if (first) {
            strlcpy(wid, s_wid[i], sizeof(wid));
            strlcpy(sentence, s_ex[i].sentence, sizeof(sentence));
        }
        r->done = true;
        r->n = n;
        r->ms = ms;
        ok = true;
        for (int k = 0; k < n; k++) {
            int o = order[k];
            if (o < 0 || o >= n) { ok = false; r->ord[k] = 0; continue; }
            r->ord[k] = (int8_t)o;
            if (strcasecmp(s_ex[i].tok[o], s_ex[i].ans[k]) != 0) ok = false;   /* cham tai may; tu lap (the, a) van dung */
        }
    }
    unlock();
    if (first) fd_sync_event_sen(wid, sentence, ok, "order");
    return ok;
}
