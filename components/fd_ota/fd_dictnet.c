#include "esp_attr.h"
/* Tra tu + luu vao bo trong 1 lan goi (lookup add:true) + lich su LittleFS. KHONG log token. */
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
#define LEGACY_Q_PATH "/data/dict_addq.jsonl"   /* hang cho cu (truoc 4.5.4): chi xoa luc init */
#define RX_CAP    12288   /* tu + vi du + toi da 32 bo */
#define NVS_NS    "fddict"

static SemaphoreHandle_t s_mx;
static QueueHandle_t s_q;
static portMUX_TYPE s_init_gate = portMUX_INITIALIZER_UNLOCKED;
static bool s_initializing;
static fd_dictnet_err_t s_init_error;
static fd_dictnet_status_t s_st;
static EXT_RAM_BSS_ATTR fd_dictnet_entry_t s_hist[FD_DICTNET_HIST_MAX];
static int s_nh;
static char s_last_cid[FD_DICTNET_CID_LEN];
static char s_last_name[FD_DICTNET_CNAME_LEN];
static char s_add_cid[FD_DICTNET_CID_LEN];
static char s_add_name[FD_DICTNET_CNAME_LEN];
static char s_add_wid[FD_DICTNET_CID_LEN];
static EXT_RAM_BSS_ATTR fd_dictnet_col_t s_cols[FD_DICTNET_COL_MAX];
static int s_ncols;
static char s_move_wid[FD_DICTNET_CID_LEN];
static char s_move_cid[FD_DICTNET_CID_LEN];
static char s_move_name[FD_DICTNET_CNAME_LEN];
static fd_dictnet_move_state_t s_move_state;
static int s_added_n;   /* so tu da luu tu lan fd_dictnet_take_added gan nhat (guarded by lock) */
/* Vi du cua lan tra gan nhat (PSRAM, guarded by lock). Tach khoi fd_dictnet_entry_t de khong phinh lich su / stack. */
static EXT_RAM_BSS_ATTR char s_ex[FD_DICTNET_EX_LEN];
static EXT_RAM_BSS_ATTR char s_exvi[FD_DICTNET_EXVI_LEN];

enum { CMD_LOOKUP = 1, CMD_MOVE };

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
    n = sizeof(s_last_name);
    nvs_get_str(h, "cname", s_last_name, &n);
    nvs_close(h);
}

/* Nho bo nguoi dung chon lan cuoi. Khong ghi de khi server tra bo cua tu da co san. */
static void save_choice(const char *cid, const char *name)
{
    if (!cid || !cid[0]) return;
    const char *nm = name ? name : "";
    if (!strcmp(s_last_cid, cid) && !strcmp(s_last_name, nm)) return;
    strlcpy(s_last_cid, cid, sizeof(s_last_cid));
    strlcpy(s_last_name, nm, sizeof(s_last_name));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "cid", s_last_cid);
    nvs_set_str(h, "cname", s_last_name);
    nvs_commit(h);
    nvs_close(h);
}

static void take_cols(cJSON *arr)
{
    s_ncols = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (s_ncols >= FD_DICTNET_COL_MAX) break;
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "id"));
        if (!id || !id[0]) continue;
        const char *nm = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(it, "name"));
        ucpy(s_cols[s_ncols].id, sizeof(s_cols[0].id), id);
        ucpy(s_cols[s_ncols].name, sizeof(s_cols[0].name), nm ? nm : "");
        s_ncols++;
    }
}

static void take_add_body(cJSON *r)
{
    const char *cid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "collection_id"));
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "collection_name"));
    const char *wid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id"));
    ucpy(s_add_cid, sizeof(s_add_cid), cid ? cid : "");
    ucpy(s_add_name, sizeof(s_add_name), name ? name : "");
    ucpy(s_add_wid, sizeof(s_add_wid), wid ? wid : "");
    take_cols(cJSON_GetObjectItemCaseSensitive(r, "collections"));
    if (!s_last_cid[0] && s_add_cid[0]) save_choice(s_add_cid, s_add_name);
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

/* cid/name co the duoc response ghi de (ten chuan tren may chu). */
static bool http_move(const char *wid, char *cid, size_t cid_n, char *name, size_t name_n)
{
    char *rx = ps_alloc(2048);
    if (!rx) return false;
    cJSON *b = cJSON_CreateObject();
    bool fields = b && cJSON_AddStringToObject(b, "id", wid) && cJSON_AddStringToObject(b, "collection_id", cid);
    int code = fields ? post_device("/api/study/dict/words/move", b, rx, 2048) : -3;
    cJSON_Delete(b);
    bool ok = false;
    if (code == 200) {
        cJSON *r = cJSON_Parse(rx);
        if (r) {
            const char *got = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "collection_id"));
            const char *nm = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "collection_name"));
            if (got && got[0]) ucpy(cid, cid_n, got);
            if (nm) ucpy(name, name_n, nm);
            ok = true;
            cJSON_Delete(r);
        }
    }
    free(rx);
    ESP_LOGI(TAG, "move %s -> %d", wid, code);
    return ok;
}

static uint8_t s_dir; /* chieu cua lookup dang xep hang (guarded by lock) */

static void do_lookup(const char *word, uint8_t dir)
{
    lock();
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = FD_DICTNET_LOOKUP;
    s_ex[0] = 0;
    s_exvi[0] = 0;
    s_ncols = 0;
    s_add_cid[0] = s_add_name[0] = s_add_wid[0] = 0;
    unlock();

    if (!fd_ota_is_paired()) {
        lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_UNPAIRED; unlock(); return;
    }
    if (!fd_net_is_connected()) {
        lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_NO_WIFI; unlock(); return;
    }

    char cid[FD_DICTNET_CID_LEN];
    lock(); ucpy(cid, sizeof(cid), s_last_cid); unlock();
    char *rx = ps_alloc(RX_CAP);
    if (!rx) { lock(); s_st.state = FD_DICTNET_ERROR; s_st.err = FD_DICTNET_ERR_NOMEM; unlock(); return; }
    /* 4.5.4: 1 lan goi duy nhat: server tra tu va luu vao bo (add:true), tra kem id/bo/danh sach bo. */
    cJSON *b = cJSON_CreateObject();
    int code = -3;
    if (b) {
        if (cJSON_AddStringToObject(b, "q", word) &&
            (dir != FD_DICTNET_DIR_VI_EN || cJSON_AddStringToObject(b, "dir", "vi-en")) &&
            cJSON_AddTrueToObject(b, "add") &&
            (!cid[0] || cJSON_AddStringToObject(b, "collection_id", cid)))
            code = post_device("/api/study/dict/lookup", b, rx, RX_CAP);
        cJSON_Delete(b);
    }
    fd_dictnet_entry_t e;
    memset(&e, 0, sizeof(e));
    fd_dictnet_err_t err = FD_DICTNET_ERR_NONE;
    bool added = false;
    if (code == 200) {
        errno = 0;
        cJSON *r = cJSON_Parse(rx);
        if (!r)
            err = errno == ENOMEM ? FD_DICTNET_ERR_NOMEM : FD_DICTNET_ERR_FORMAT;
        else if (!parse_entry(r, &e) || !e.vi[0])
            err = FD_DICTNET_ERR_NOT_FOUND;
        else {
            /* Server (ham chung completeWordInfo) da lam day; thiet bi chi hien. */
            /* Server cu khong biet add -> khong co "added": tu khong duoc luu (deploy web-app truoc). */
            added = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "added"));
            lock();
            ucpy(s_ex, sizeof(s_ex), jstr(r, "example"));
            ucpy(s_exvi, sizeof(s_exvi), s_ex[0] ? jstr(r, "example_vi") : "");
            if (added) {
                take_add_body(r);
                /* state=same: tu da co trong bo, khong can dong bo lai luc thoat Tu dien. Server cu khong gui state -> van dem. */
                const char *st = jstr(r, "state");
                if (!st[0] || strcmp(st, "same") != 0) s_added_n++;
            }
            unlock();
        }
        cJSON_Delete(r);
    } else if (code == 404) err = FD_DICTNET_ERR_NOT_FOUND;
    else if (code == 401) err = FD_DICTNET_ERR_UNPAIRED;
    else if (code == -3) err = FD_DICTNET_ERR_NOMEM;
    else if (code < 0) err = FD_DICTNET_ERR_NET;
    else err = FD_DICTNET_ERR_SERVER;

    if (!err) {
        e.dir = dir;
        err = hist_push(&e);
    }

    lock();
    s_st.http = code;
    s_st.err = err;
    s_st.result = e;
    s_st.added = added;
    if (!err && added) {
        ucpy(s_st.word_id, sizeof(s_st.word_id), s_add_wid);
        ucpy(s_st.collection_id, sizeof(s_st.collection_id), s_add_cid);
        ucpy(s_st.collection_name, sizeof(s_st.collection_name), s_add_name);
    }
    s_st.state = err ? FD_DICTNET_ERROR : FD_DICTNET_OK;
    unlock();
    free(rx);
    ESP_LOGI(TAG, "lookup %s -> %d err=%d added=%d", word, code, err, added);
}

static void task(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* resources published atomically before wake */
    for (;;) {
        int cmd = 0;
        if (xQueueReceive(s_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd == CMD_MOVE) {
            char wid[FD_DICTNET_CID_LEN], cid[FD_DICTNET_CID_LEN], name[FD_DICTNET_CNAME_LEN];
            lock();
            ucpy(wid, sizeof(wid), s_move_wid);
            ucpy(cid, sizeof(cid), s_move_cid);
            ucpy(name, sizeof(name), s_move_name);
            unlock();
            bool ok = true;
            if (wid[0]) ok = http_move(wid, cid, sizeof(cid), name, sizeof(name));
            lock();
            if (ok) save_choice(cid, name);
            if (ok) {
                ucpy(s_move_cid, sizeof(s_move_cid), cid);
                ucpy(s_move_name, sizeof(s_move_name), name);
                if (s_st.state == FD_DICTNET_OK) {
                    ucpy(s_st.collection_id, sizeof(s_st.collection_id), cid);
                    ucpy(s_st.collection_name, sizeof(s_st.collection_name), name);
                }
            }
            s_move_state = ok ? FD_DICTNET_MOVE_OK : FD_DICTNET_MOVE_ERR;
            unlock();
        } else if (cmd == CMD_LOOKUP) {
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
    /* 4.5.4: bo hang cho offline; xoa file cu neu con (best effort, bo qua loi). */
    unlink(LEGACY_Q_PATH);
    unlink(LEGACY_Q_PATH ".tmp");
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
    if (s_st.state == FD_DICTNET_LOOKUP) { unlock(); return false; }
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

void fd_dictnet_get_example(char *ex, size_t ex_cap, char *ex_vi, size_t vi_cap)
{
    if (ex && ex_cap) ex[0] = 0;
    if (ex_vi && vi_cap) ex_vi[0] = 0;
    if (!s_mx) return;
    lock();
    if (ex && ex_cap) ucpy(ex, ex_cap, s_ex);
    if (ex_vi && vi_cap) ucpy(ex_vi, vi_cap, s_exvi);
    unlock();
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

int fd_dictnet_take_added(void)
{
    if (!s_mx) return 0;
    lock();
    int n = s_added_n;
    s_added_n = 0;
    unlock();
    return n;
}

int fd_dictnet_cols(fd_dictnet_col_t *out, int cap)
{
    if (!out || cap <= 0 || !s_mx) return 0;
    lock();
    int n = s_ncols < cap ? s_ncols : cap;
    for (int i = 0; i < n; i++) out[i] = s_cols[i];
    unlock();
    return n;
}

void fd_dictnet_saved_collection(char *id, size_t id_cap, char *name, size_t name_cap)
{
    if (id && id_cap) id[0] = 0;
    if (name && name_cap) name[0] = 0;
    if (!s_mx) return;
    lock();
    if (id && id_cap) ucpy(id, id_cap, s_last_cid);
    if (name && name_cap) ucpy(name, name_cap, s_last_name);
    unlock();
}

bool fd_dictnet_move(const char *wid, const char *cid, const char *name)
{
    if (!cid || !cid[0] || !fd_dictnet_init()) return false;
    lock();
    if (s_move_state == FD_DICTNET_MOVE_BUSY || s_st.state == FD_DICTNET_LOOKUP) {
        unlock();
        return false;
    }
    ucpy(s_move_wid, sizeof(s_move_wid), wid ? wid : "");
    ucpy(s_move_cid, sizeof(s_move_cid), cid);
    ucpy(s_move_name, sizeof(s_move_name), name ? name : "");
    s_move_state = FD_DICTNET_MOVE_BUSY;
    unlock();
    int cmd = CMD_MOVE;
    if (xQueueSend(s_q, &cmd, 0) == pdTRUE) return true;
    lock();
    s_move_state = FD_DICTNET_MOVE_ERR;
    unlock();
    return false;
}

fd_dictnet_move_state_t fd_dictnet_move_state(char *id, size_t id_cap, char *name, size_t name_cap)
{
    if (!s_mx) return FD_DICTNET_MOVE_IDLE;
    lock();
    fd_dictnet_move_state_t st = s_move_state;
    if (id && id_cap) ucpy(id, id_cap, s_move_cid);
    if (name && name_cap) ucpy(name, name_cap, s_move_name);
    unlock();
    return st;
}

void fd_dictnet_move_ack(void)
{
    if (!s_mx) return;
    lock();
    if (s_move_state == FD_DICTNET_MOVE_OK || s_move_state == FD_DICTNET_MOVE_ERR)
        s_move_state = FD_DICTNET_MOVE_IDLE;
    unlock();
}
