/* Tra tu proxy + lich su LittleFS + hang cho words/add. KHONG log token. */
#include "fd_dictnet.h"
#include <stdio.h>
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
static fd_dictnet_status_t s_st;
static fd_dictnet_entry_t s_hist[FD_DICTNET_HIST_MAX];
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
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
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

static void hist_load(void)
{
    s_nh = 0;
    FILE *f = fopen(HIST_PATH, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024) { fclose(f); return; }
    char *b = ps_alloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return; }
    fclose(f);
    b[n] = 0;
    cJSON *r = cJSON_Parse(b);
    free(b);
    if (!r || !cJSON_IsArray(r)) { cJSON_Delete(r); return; }
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
    }
    cJSON_Delete(r);
}

static void hist_save(void)
{
    cJSON *a = cJSON_CreateArray();
    if (!a) return;
    for (int i = 0; i < s_nh; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) continue;
        cJSON_AddStringToObject(o, "en", s_hist[i].en);
        cJSON_AddStringToObject(o, "vi", s_hist[i].vi);
        cJSON_AddStringToObject(o, "ipa", s_hist[i].ipa);
        cJSON_AddStringToObject(o, "pos", s_hist[i].pos);
        cJSON_AddItemToArray(a, o);
    }
    char *j = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    if (!j) return;
    mkdir("/data", 0755);
    FILE *f = fopen(HIST_PATH ".tmp", "wb");
    if (f) {
        fwrite(j, 1, strlen(j), f);
        fclose(f);
        unlink(HIST_PATH);
        rename(HIST_PATH ".tmp", HIST_PATH);
    }
    free(j);
}

static void hist_push(const fd_dictnet_entry_t *e)
{
    if (!e || !e->en[0]) return;
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
    hist_save();
}

static void queue_append(const fd_dictnet_entry_t *e)
{
    if (!e || !e->en[0] || !e->vi[0]) return;
    mkdir("/data", 0755);
    FILE *f = fopen(Q_PATH, "ab");
    if (!f) return;
    cJSON *o = cJSON_CreateObject();
    if (!o) { fclose(f); return; }
    cJSON_AddStringToObject(o, "en", e->en);
    cJSON_AddStringToObject(o, "vi", e->vi);
    cJSON_AddStringToObject(o, "ipa", e->ipa);
    cJSON_AddStringToObject(o, "pos", e->pos);
    if (s_last_cid[0]) cJSON_AddStringToObject(o, "collection_id", s_last_cid);
    char *j = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (j) { fprintf(f, "%s\n", j); free(j); }
    fclose(f);
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
    cJSON_AddStringToObject(body, "device_id", id);
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
        cJSON_AddStringToObject(b, "en", e->en);
        cJSON_AddStringToObject(b, "vi", e->vi);
        cJSON_AddStringToObject(b, "word", e->en);
        if (e->ipa[0]) cJSON_AddStringToObject(b, "ipa", e->ipa);
        if (e->pos[0]) cJSON_AddStringToObject(b, "pos", e->pos);
        if (s_last_cid[0]) cJSON_AddStringToObject(b, "collection_id", s_last_cid);
        int code = post_device("/api/study/dict/words/add", b, rx, 2048);
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
    char line[512];
    char keep[64 * 1024];
    size_t kn = 0;
    keep[0] = 0;
    while (fgets(line, sizeof(line), f)) {
        cJSON *o = cJSON_Parse(line);
        fd_dictnet_entry_t e;
        bool ok = false;
        if (o && parse_entry(o, &e)) ok = do_add(&e);
        cJSON_Delete(o);
        if (!ok) {
            size_t L = strlen(line);
            if (kn + L + 1 < sizeof(keep)) { memcpy(keep + kn, line, L); kn += L; if (kn && keep[kn - 1] != '\n') keep[kn++] = '\n'; keep[kn] = 0; }
        }
    }
    fclose(f);
    if (kn == 0) unlink(Q_PATH);
    else {
        FILE *w = fopen(Q_PATH ".tmp", "wb");
        if (w) { fwrite(keep, 1, kn, w); fclose(w); unlink(Q_PATH); rename(Q_PATH ".tmp", Q_PATH); }
    }
}

static void do_lookup(const char *word)
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
    if (!rx) { lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_NET; unlock(); return; }
    cJSON *b = cJSON_CreateObject();
    int code = -1;
    if (b) {
        cJSON_AddStringToObject(b, "q", word);
        code = post_device("/api/study/dict/lookup", b, rx, 4096);
        cJSON_Delete(b);
    }
    fd_dictnet_entry_t e;
    memset(&e, 0, sizeof(e));
    fd_dictnet_err_t err = FD_DICTNET_ERR_NONE;
    if (code == 200) {
        cJSON *r = cJSON_Parse(rx);
        if (!r || !parse_entry(r, &e) || !e.vi[0]) err = FD_DICTNET_ERR_FORMAT;
        cJSON_Delete(r);
    } else if (code == 404) err = FD_DICTNET_ERR_NOT_FOUND;
    else if (code == 401) err = FD_DICTNET_ERR_UNPAIRED;
    else if (code < 0) err = FD_DICTNET_ERR_NET;
    else err = FD_DICTNET_ERR_SERVER;

    bool added = false, queued = false;
    if (!err) {
        hist_push(&e);
        lock(); s_st.state = FD_DICTNET_ADD; unlock();
        added = do_add(&e);
        if (!added) { queue_append(&e); queued = true; }
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
    for (;;) {
        int cmd = 0;
        if (xQueueReceive(s_q, &cmd, pdMS_TO_TICKS(30000)) != pdTRUE) {
            flush_queue();
            continue;
        }
        if (cmd == CMD_FLUSH) flush_queue();
        else if (cmd == CMD_LOOKUP) {
            char word[FD_DICTNET_WORD_LEN];
            lock(); ucpy(word, sizeof(word), s_st.result.en); unlock();
            /* word stashed in result.en before queueing */
            do_lookup(word);
        }
    }
}

void fd_dictnet_init(void)
{
    if (s_mx) return;
    s_mx = xSemaphoreCreateMutex();
    s_q = xQueueCreate(4, sizeof(int));
    load_nvs();
    hist_load();
    mkdir("/data", 0755);
    xTaskCreate(task, "fd_dict", 6144, NULL, 4, NULL);
}

bool fd_dictnet_lookup(const char *word)
{
    if (!word || !word[0]) return false;
    fd_dictnet_init();
    lock();
    if (s_st.state == FD_DICTNET_LOOKUP || s_st.state == FD_DICTNET_ADD) { unlock(); return false; }
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = FD_DICTNET_LOOKUP;
    ucpy(s_st.result.en, sizeof(s_st.result.en), word);
    unlock();
    int cmd = CMD_LOOKUP;
    return xQueueSend(s_q, &cmd, 0) == pdTRUE;
}

void fd_dictnet_get(fd_dictnet_status_t *out)
{
    if (!out) return;
    fd_dictnet_init();
    lock(); *out = s_st; unlock();
}

int fd_dictnet_history(fd_dictnet_entry_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    fd_dictnet_init();
    lock();
    int n = s_nh < cap ? s_nh : cap;
    for (int i = 0; i < n; i++) out[i] = s_hist[i];
    unlock();
    return n;
}

void fd_dictnet_flush(void)
{
    fd_dictnet_init();
    int cmd = CMD_FLUSH;
    xQueueSend(s_q, &cmd, 0);
}
