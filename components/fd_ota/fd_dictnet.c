#include "esp_attr.h"
/* Tra tu proxy + lich su LittleFS + hang cho words/add. KHONG log token. */
#include "fd_dictnet.h"
#include <stdio.h>
#include <errno.h>
#include "fd_store.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "fd_dict";
#define HIST_PATH "/data/dict_hist.json"
#define Q_PATH    "/data/dict_addq.jsonl"
#define NVS_NS    "fddict"

static SemaphoreHandle_t s_mx;
static QueueHandle_t s_q;
static portMUX_TYPE s_init_gate = portMUX_INITIALIZER_UNLOCKED;
static bool s_initializing;
static fd_dictnet_err_t s_init_error;
static fd_dictnet_status_t s_st;
static EXT_RAM_BSS_ATTR fd_dictnet_entry_t s_hist[FD_DICTNET_HIST_MAX];
static int s_nh;
static char s_last_cid[25];

enum { CMD_LOOKUP = 1, CMD_FLUSH };

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

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

static void *ps_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static const char *jstr(cJSON *o, const char *k)
{
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o, k));
    return s ? s : "";
}

static void load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(s_last_cid);
    nvs_get_str(h, "cid", s_last_cid, &n);
    nvs_close(h);
}

static void save_cid(const char *cid)
{
    if (!cid || !cid[0]) return;
    if (!strcmp(s_last_cid, cid)) return;
    strlcpy(s_last_cid, cid, sizeof(s_last_cid));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "cid", s_last_cid);
    nvs_commit(h);
    nvs_close(h);
}

static fd_dictnet_err_t hist_load(void)
{
    s_nh = 0;
    FILE *f = fopen(HIST_PATH, "rb");
    if (!f) return errno == ENOENT ? FD_DICTNET_ERR_NONE : FD_DICTNET_ERR_STORAGE;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024) { fclose(f); return FD_DICTNET_ERR_FORMAT; }
    char *b = ps_alloc((size_t)n + 1);
    if (!b) { fclose(f); return FD_DICTNET_ERR_NOMEM; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return FD_DICTNET_ERR_FORMAT; }
    fclose(f);
    b[n] = 0;
    errno = 0;
    cJSON *r = cJSON_Parse(b);
    bool parse_oom = !r && errno == ENOMEM;
    free(b);
    if (!r || !cJSON_IsArray(r)) { cJSON_Delete(r); return parse_oom ? FD_DICTNET_ERR_NOMEM : FD_DICTNET_ERR_FORMAT; }
    cJSON *it;
    cJSON_ArrayForEach(it, r) {
        if (s_nh >= FD_DICTNET_HIST_MAX) break;
        const char *en = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "en"));
        const char *vi = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "vi"));
        if (!en || !en[0]) continue;
        fd_dictnet_entry_t *e = &s_hist[s_nh++];
        memset(e, 0, sizeof(*e));
        ucpy(e->en, sizeof(e->en), en);
        ucpy(e->vi, sizeof(e->vi), vi ? vi : "");
        ucpy(e->ipa, sizeof(e->ipa), jstr(it, "ipa"));
        ucpy(e->pos, sizeof(e->pos), jstr(it, "pos"));
        e->dir = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "d")) ? FD_DICTNET_DIR_VI_EN : FD_DICTNET_DIR_EN_VI;
    }
    cJSON_Delete(r);
    return FD_DICTNET_ERR_NONE;
}

/* Never publish a partial JSON document or remove the last good file first. */
static fd_dictnet_err_t write_atomic(const char *path, const char *tmp, const char *data, size_t n)
{
    FILE *f = fopen(tmp, "wb");
    if (!f) return FD_DICTNET_ERR_STORAGE;
    bool ok = fwrite(data, 1, n, f) == n;
    if (fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return FD_DICTNET_ERR_STORAGE;
    }
    return FD_DICTNET_ERR_NONE;
}

static cJSON *entry_json(const fd_dictnet_entry_t *e)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    if (!cJSON_AddStringToObject(o, "en", e->en) ||
        !cJSON_AddStringToObject(o, "vi", e->vi) ||
        !cJSON_AddStringToObject(o, "ipa", e->ipa) ||
        !cJSON_AddStringToObject(o, "pos", e->pos) ||
        (e->dir == FD_DICTNET_DIR_VI_EN && !cJSON_AddTrueToObject(o, "d"))) {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}

static fd_dictnet_err_t hist_save(void)
{
    cJSON *a = cJSON_CreateArray();
    if (!a) return FD_DICTNET_ERR_NOMEM;
    for (int i = 0; i < s_nh; i++) {
        cJSON *o = entry_json(&s_hist[i]);
        if (!o || !cJSON_AddItemToArray(a, o)) {
            cJSON_Delete(o);
            cJSON_Delete(a);
            return FD_DICTNET_ERR_NOMEM;
        }
    }
    char *j = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    if (!j) return FD_DICTNET_ERR_NOMEM;
    fd_dictnet_err_t err = write_atomic(HIST_PATH, HIST_PATH ".tmp", j, strlen(j));
    cJSON_free(j);
    return err;
}
void fd_dictnet_clear_history(void)
{
    if (s_mx) lock();
    s_nh = 0;
    memset(s_hist, 0, sizeof(s_hist));
    unlink(HIST_PATH);
    unlink(HIST_PATH ".tmp");
    if (s_mx) unlock();
    ESP_LOGI(TAG, "xoa lich su tra");
}

static fd_dictnet_err_t hist_push(const fd_dictnet_entry_t *e)
{
    if (!e || !e->en[0]) return FD_DICTNET_ERR_FORMAT;
    lock();
    /* dedupe: dua len dau */
    int found = -1;
    for (int i = 0; i < s_nh; i++) if (!strcasecmp(s_hist[i].en, e->en)) { found = i; break; }
    fd_dictnet_entry_t tmp = *e;
    if (found >= 0) {
        for (int i = found; i > 0; i--) s_hist[i] = s_hist[i - 1];
        s_hist[0] = tmp;
    } else {
        if (s_nh < FD_DICTNET_HIST_MAX) s_nh++;
        for (int i = s_nh - 1; i > 0; i--) s_hist[i] = s_hist[i - 1];
        s_hist[0] = tmp;
    }
    unlock();
    return hist_save();
}

static fd_dictnet_err_t queue_append(const fd_dictnet_entry_t *e)
{
    if (!e || !e->en[0] || !e->vi[0]) return FD_DICTNET_ERR_FORMAT;
    cJSON *o = entry_json(e);
    if (!o) return FD_DICTNET_ERR_NOMEM;
    if (s_last_cid[0] && !cJSON_AddStringToObject(o, "collection_id", s_last_cid)) {
        cJSON_Delete(o);
        return FD_DICTNET_ERR_NOMEM;
    }
    char *j = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!j) return FD_DICTNET_ERR_NOMEM;
    /* Copy the old queue before appending: failed writes/OOM keep it intact. */
    FILE *src = fopen(Q_PATH, "rb");
    if (!src && errno != ENOENT) { cJSON_free(j); return FD_DICTNET_ERR_STORAGE; }
    FILE *dst = fopen(Q_PATH ".tmp", "wb");
    bool ok = dst != NULL;
    char chunk[256];
    if (src) {
        size_t n;
        while (ok && (n = fread(chunk, 1, sizeof(chunk), src)) > 0)
            ok = fwrite(chunk, 1, n, dst) == n;
        if (ferror(src)) ok = false;
        fclose(src);
    }
    if (dst) {
        size_t n = strlen(j);
        if (ok) ok = fwrite(j, 1, n, dst) == n && fputc('\n', dst) != EOF;
        if (fflush(dst) != 0) ok = false;
        if (fsync(fileno(dst)) != 0) ok = false;
        if (fclose(dst) != 0) ok = false;
    }
    cJSON_free(j);
    if (!ok || rename(Q_PATH ".tmp", Q_PATH) != 0) {
        unlink(Q_PATH ".tmp");
        return FD_DICTNET_ERR_STORAGE;
    }
    return FD_DICTNET_ERR_NONE;
}
static bool parse_entry(cJSON *r, fd_dictnet_entry_t *e)
{
    memset(e, 0, sizeof(*e));
    const char *en = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "en"));
    if (!en) en = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "word"));
    const char *vi = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "vi"));
    if (!en || !en[0]) return false;
    ucpy(e->en, sizeof(e->en), en);
    ucpy(e->vi, sizeof(e->vi), vi ? vi : "");
    ucpy(e->ipa, sizeof(e->ipa), jstr(r, "ipa"));
    ucpy(e->pos, sizeof(e->pos), jstr(r, "pos"));
    return true;
}

static int post_device(const char *path, cJSON *body, char *resp, int cap)
{
    char id[16];
    fd_ota_get_device_id(id, sizeof(id));
    if (!cJSON_AddStringToObject(body, "device_id", id)) return -3;
    char *j = cJSON_PrintUnformatted(body);
    if (!j) return -3;
    int code = fd_ota_api_post(path, j, resp, cap);
    free(j);
    return code;
}

static bool do_add(const fd_dictnet_entry_t *e)
{
    if (!e->vi[0]) return false;
    char *rx = ps_alloc(2048);
    if (!rx) return false;
    cJSON *b = cJSON_CreateObject();
    bool ok = false;
    if (b) {
        bool fields = cJSON_AddStringToObject(b, "en", e->en) &&
                      cJSON_AddStringToObject(b, "vi", e->vi) &&
                      cJSON_AddStringToObject(b, "word", e->en) &&
                      (!e->ipa[0] || cJSON_AddStringToObject(b, "ipa", e->ipa)) &&
                      (!e->pos[0] || cJSON_AddStringToObject(b, "pos", e->pos)) &&
                      (!s_last_cid[0] || cJSON_AddStringToObject(b, "collection_id", s_last_cid));
        int code = fields ? post_device("/api/study/dict/words/add", b, rx, 2048) : -3;
        cJSON_Delete(b);
        if (code == 200) {
            cJSON *r = cJSON_Parse(rx);
            if (r) {
                const char *cid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "collection_id"));
                if (cid) save_cid(cid);
                ok = true;
                cJSON_Delete(r);
            }
        } else if (code == 401) {
            /* token xoa boi fd_ota */
        }
        ESP_LOGI(TAG, "add %s -> %d", e->en, code);
    }
    free(rx);
    return ok;
}

static void flush_queue(void)
{
    if (!fd_net_is_connected() || !fd_ota_is_paired()) return;
    FILE *f = fopen(Q_PATH, "rb");
    if (!f) return;
    const size_t KEEP_CAP = 64 * 1024;
    char *keep = ps_alloc(KEEP_CAP);
    char *line = ps_alloc(4096);
    if (!keep || !line) { free(keep); free(line); fclose(f); return; }
    size_t kn = 0;
    bool complete = true;
    while (fgets(line, 4096, f)) {
        size_t len = strlen(line);
        /* A truncated/invalid line or OOM must never discard queued words. */
        if (!len || line[len - 1] != '\n') { complete = false; break; }
        cJSON *o = cJSON_Parse(line);
        if (!o) { complete = false; break; }
        fd_dictnet_entry_t e;
        bool ok = parse_entry(o, &e) && do_add(&e);
        cJSON_Delete(o);
        if (!ok) {
            if (kn + len >= KEEP_CAP) { complete = false; break; }
            memcpy(keep + kn, line, len);
            kn += len;
        }
    }
    if (ferror(f)) complete = false;
    fclose(f);
    if (complete && write_atomic(Q_PATH, Q_PATH ".tmp", keep, kn) != FD_DICTNET_ERR_NONE)
        ESP_LOGW(TAG, "queue write failed; original preserved");
    free(line);
    free(keep);
}
static uint8_t s_dir; /* chieu cua lookup dang xep hang (guarded by lock) */

static void do_lookup(const char *word, uint8_t dir)
{
    lock();
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = FD_DICTNET_LOOKUP;
    unlock();

    if (!fd_ota_is_paired()) {
        lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_UNPAIRED; unlock(); return;
    }
    if (!fd_net_is_connected()) {
        lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_NO_WIFI; unlock(); return;
    }

    char *rx = ps_alloc(4096);
    if (!rx) { lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_NOMEM; unlock(); return; }
    cJSON *b = cJSON_CreateObject();
    int code = -3;
    if (b) {
        if (cJSON_AddStringToObject(b, "q", word) &&
            (dir != FD_DICTNET_DIR_VI_EN || cJSON_AddStringToObject(b, "dir", "vi-en")))
            code = post_device("/api/study/dict/lookup", b, rx, 4096);
        cJSON_Delete(b);
    }
    fd_dictnet_entry_t e;
    memset(&e, 0, sizeof(e));
    fd_dictnet_err_t err = FD_DICTNET_ERR_NONE;
    if (code == 200) {
        errno = 0;
        cJSON *r = cJSON_Parse(rx);
        if (!r)
            err = errno == ENOMEM ? FD_DICTNET_ERR_NOMEM : FD_DICTNET_ERR_FORMAT;
        else if (!parse_entry(r, &e) || !e.vi[0])
            err = FD_DICTNET_ERR_NOT_FOUND;
        cJSON_Delete(r);
    } else if (code == 404) err = FD_DICTNET_ERR_NOT_FOUND;
    else if (code == 401) err = FD_DICTNET_ERR_UNPAIRED;
    else if (code == -3) err = FD_DICTNET_ERR_NOMEM;
    else if (code < 0) err = FD_DICTNET_ERR_NET;
    else err = FD_DICTNET_ERR_SERVER;

    bool added = false, queued = false;
    if (!err) {
        e.dir = dir;
        fd_dictnet_err_t persist_err = hist_push(&e);
        lock(); s_st.state = FD_DICTNET_ADD; unlock();
        added = do_add(&e);
        if (!added) {
            fd_dictnet_err_t qe = queue_append(&e);
            queued = qe == FD_DICTNET_ERR_NONE;
            if (qe) persist_err = qe;
        }
        err = persist_err;
    }

    lock();
    s_st.http = code;
    s_st.err = err;
    s_st.result = e;
    s_st.added = added;
    s_st.queued = queued;
    s_st.state = err ? FD_DICTNET_ERROR : FD_DICTNET_OK;
    unlock();
    free(rx);
    ESP_LOGI(TAG, "lookup %s -> %d err=%d added=%d queued=%d", word, code, err, added, queued);
}

static void task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* resources published atomically before wake */
    for (;;) {
        int cmd = 0;
        if (xQueueReceive(s_q, &cmd, pdMS_TO_TICKS(30000)) != pdTRUE) {
            flush_queue();
            continue;
        }
        if (cmd == CMD_FLUSH) flush_queue();
        else if (cmd == CMD_LOOKUP) {
            char word[FD_DICTNET_WORD_LEN];
            uint8_t dir;
            lock(); ucpy(word, sizeof(word), s_st.result.en); dir = s_dir; unlock();
            /* word stashed in result.en before queueing */
            do_lookup(word, dir);
        }
    }
}

bool fd_dictnet_init(void)
{
    taskENTER_CRITICAL(&s_init_gate);
    if (s_mx) { taskEXIT_CRITICAL(&s_init_gate); return true; }
    if (s_initializing) { taskEXIT_CRITICAL(&s_init_gate); return false; }
    s_initializing = true;
    taskEXIT_CRITICAL(&s_init_gate);

    fd_dictnet_err_t error = FD_DICTNET_ERR_NOMEM;
    SemaphoreHandle_t mx = xSemaphoreCreateMutex();
    QueueHandle_t queue = mx ? xQueueCreate(4, sizeof(int)) : NULL;
    TaskHandle_t worker = NULL;
    if (!mx || !queue) goto failed;
    load_nvs();
    if (fd_store_status() != ESP_OK) { error = FD_DICTNET_ERR_STORAGE; goto failed; }
    error = hist_load();
    if (error != FD_DICTNET_ERR_NONE) goto failed;   /* preserve history on OOM/parse failure */
    error = FD_DICTNET_ERR_NOMEM;
    if (xTaskCreate(task, "fd_dict", 8192, NULL, 4, &worker) != pdPASS) goto failed;
    taskENTER_CRITICAL(&s_init_gate);
    s_q = queue;
    s_mx = mx;
    s_init_error = FD_DICTNET_ERR_NONE;
    s_initializing = false;
    taskEXIT_CRITICAL(&s_init_gate);
    xTaskNotifyGive(worker);
    return true;

failed:
    if (queue) vQueueDelete(queue);
    if (mx) vSemaphoreDelete(mx);
    taskENTER_CRITICAL(&s_init_gate);
    s_init_error = error;
    s_initializing = false;
    taskEXIT_CRITICAL(&s_init_gate);
    ESP_LOGW(TAG, "init error=%d: resources released, retry available", error);
    return false;
}

bool fd_dictnet_lookup(const char *word)
{
    return fd_dictnet_lookup_dir(word, FD_DICTNET_DIR_EN_VI);
}

bool fd_dictnet_lookup_dir(const char *word, uint8_t dir)
{
    if (!word || !word[0]) return false;
    if (!fd_dictnet_init()) return false;
    lock();
    if (s_st.state == FD_DICTNET_LOOKUP || s_st.state == FD_DICTNET_ADD) { unlock(); return false; }
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = FD_DICTNET_LOOKUP;
    ucpy(s_st.result.en, sizeof(s_st.result.en), word);
    s_dir = dir;
    unlock();
    int cmd = CMD_LOOKUP;
    if (xQueueSend(s_q, &cmd, 0) == pdTRUE) return true;
    lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_NOMEM; unlock();
    return false;
}

void fd_dictnet_get(fd_dictnet_status_t *out)
{
    if (!out) return;
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        out->state = s_init_error ? FD_DICTNET_ERROR : FD_DICTNET_IDLE;
        out->err = s_init_error;
        return;
    }
    lock(); *out = s_st; unlock();
}

int fd_dictnet_history(fd_dictnet_entry_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    if (!fd_dictnet_init()) return 0;
    lock();
    int n = s_nh < cap ? s_nh : cap;
    for (int i = 0; i < n; i++) out[i] = s_hist[i];
    unlock();
    return n;
}

void fd_dictnet_flush(void)
{
    if (!fd_dictnet_init()) return;
    int cmd = CMD_FLUSH;
    xQueueSend(s_q, &cmd, 0);
}
