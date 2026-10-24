/* Du lieu offline cho Hoc tap: xem fd_deck.h. KHONG log token / header / body.
 * 23/10: bo /api/v1/decks, words.tsv, audio.pak (server khong co). Nguon offline = cache cac phan hoi that cua
 * POST /api/study/start (/data/study_cache/<seq 8 hex><e|v>.json, .usd = phien da gui ket qua online).
 * Ket qua offline -> /data/outbox -> POST /api/study/result (cung dinh dang phien online). */
#include "fd_deck.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_deck";

#define CDIR       "/data/study_cache"
#define OBOX       "/data/outbox"
#define CACHE_MAX_FILES 16
#define CACHE_MAX_BYTES (512 * 1024)
#define FILE_MAX   (24 * 1024)
#define OBOX_MAX   50
#define OBOX_MAX_BYTES (512 * 1024)   /* tran: bo ban CU nhat, giu ket qua moi */
#define OBOX_BACKOFF_MIN_US (30LL * 1000000)
#define OBOX_BACKOFF_MAX_US (30LL * 60 * 1000000)
#define BODY_MAX   8192
#define SYNC_MS    60000
#define POLL_MS    5000
#define PATH_N     96

enum { CMD_SCAN = 1, CMD_SYNC, CMD_REFILL };

static QueueHandle_t s_q;
static SemaphoreHandle_t s_mx;
static fd_deck_status_t s_st;

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static char *read_file(const char *p, size_t max, size_t *out_len)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = NULL;
    if (n > 0 && (size_t)n <= max) {
        b = heap_caps_malloc((size_t)n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!b) b = malloc((size_t)n + 1);
        if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
        if (b) b[n] = 0;
    }
    fclose(f);
    if (b && out_len) *out_len = (size_t)n;
    return b;
}

static bool write_atomic(const char *tmp, const char *dst, const char *data)
{
    FILE *f = fopen(tmp, "w");
    bool ok = f && fputs(data, f) >= 0;
    if (f) {
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;   /* du lieu xuong flash truoc khi rename */
        ok = (fclose(f) == 0) && ok;
    }
    /* littlefs: rename de len file cu la nguyen tu (mat dien giua chung -> con ban cu hoac ban moi, khong mat ca hai) */
    ok = ok && rename(tmp, dst) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

/* ---------- cache ---------- */
typedef struct { char name[24]; uint32_t seq; char dir; bool used; long size; } cent_t;

static bool cname(const char *n, cent_t *c)   /* "<8 hex><e|v>.json|.usd" */
{
    if (strlen(n) != 14 || n[9] != '.') return false;
    bool used = !strcmp(n + 9, ".usd");
    if (!used && strcmp(n + 9, ".json")) return false;
    if (strlen(n) != 14 && !used) return false;
    char h[9];
    memcpy(h, n, 8); h[8] = 0;
    char *e;
    uint32_t s = (uint32_t)strtoul(h, &e, 16);
    if (*e || (n[8] != 'e' && n[8] != 'v')) return false;
    strlcpy(c->name, n, sizeof(c->name));
    c->seq = s; c->dir = n[8]; c->used = used; c->size = 0;
    return true;
}
/* ".json" = 5 ky tu, ".usd" = 4: ten 14 / 13 */
static bool cname_any(const char *n, cent_t *c)
{
    size_t l = strlen(n);
    if (l == 14) return cname(n, c);
    if (l == 13 && !strcmp(n + 9, ".usd")) {
        char h[9]; memcpy(h, n, 8); h[8] = 0; char *e;
        uint32_t s = (uint32_t)strtoul(h, &e, 16);
        if (*e || (n[8] != 'e' && n[8] != 'v')) return false;
        strlcpy(c->name, n, sizeof(c->name)); c->seq = s; c->dir = n[8]; c->used = true; c->size = 0;
        return true;
    }
    return false;
}

static int cache_list(cent_t *out, int cap, long *total)
{
    if (total) *total = 0;
    DIR *d = opendir(CDIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    char p[PATH_N + 32];
    while ((e = readdir(d)) != NULL) {
        cent_t c;
        if (!cname_any(e->d_name, &c)) continue;
        snprintf(p, sizeof(p), CDIR "/%.23s", c.name);
        struct stat st;
        if (stat(p, &st) == 0) c.size = st.st_size;
        if (total) *total += c.size;
        if (n < cap) out[n++] = c;
    }
    closedir(d);
    return n;
}

/* Dem cau trong cache (chi dem "\"answer\"" nhanh, khong parse) */
static void cache_count(void)
{
    cent_t L[CACHE_MAX_FILES + 8];
    long tot;
    int n = cache_list(L, CACHE_MAX_FILES + 8, &tot);
    int qn = 0, fresh = 0;
    char p[PATH_N + 32];
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof(p), CDIR "/%.23s", L[i].name);
        char *b = read_file(p, FILE_MAX, NULL);
        if (!b) continue;
        for (const char *s = b; (s = strstr(s, "\"answer\"")) != NULL; s += 8) qn++;
        free(b);
        if (!L[i].used) fresh++;
    }
    lock();
    s_st.have = qn > 0;
    s_st.words = qn;
    s_st.version = n;
    s_st.up_to_date = fresh > 0;
    s_st.state = FD_DECK_DONE;
    s_st.pct = 100;
    s_st.err[0] = 0;
    strlcpy(s_st.id, "study_cache", sizeof(s_st.id));
    strlcpy(s_st.name, "study/start", sizeof(s_st.name));
    unlock();
    ESP_LOGI(TAG, "study cache: %d phien, %d cau, %ld B (%d chua gui)", n, qn, tot, fresh);
}

/* So phien chua dung (chua gui ket qua) theo chieu ('e' / 'v'); chi liet ke thu muc, khong doc file. */
static int fresh_count(char dir)
{
    cent_t L[CACHE_MAX_FILES + 8];
    int n = cache_list(L, CACHE_MAX_FILES + 8, NULL), f = 0;
    for (int i = 0; i < n; i++) f += !L[i].used && L[i].dir == dir;
    return f;
}

bool fd_deck_has_fresh(bool vi_en) { return s_mx && fresh_count(vi_en ? 'v' : 'e') > 0; }

void fd_deck_refill(void)
{
    if (s_q) { int c = CMD_REFILL; xQueueSend(s_q, &c, 0); }
}

void fd_deck_cache_add(const char *direction, const char *json)
{
    if (!json || !s_mx) return;
    size_t len = strlen(json);
    if (len == 0 || len >= FILE_MAX) { ESP_LOGI(TAG, "study cache: bo qua (%u B)", (unsigned)len); return; }
    char dch = (direction && !strcmp(direction, "vi-en")) ? 'v' : 'e';
    mkdir(CDIR, 0775);
    cent_t L[CACHE_MAX_FILES + 8];
    long tot;
    int n = cache_list(L, CACHE_MAX_FILES + 8, &tot);
    uint32_t mx = 0;
    for (int i = 0; i < n; i++) if (L[i].seq > mx) mx = L[i].seq;
    /* xoa cu nhat (uu tien phien da gui) den khi du cho */
    while (n > 0 && (n >= CACHE_MAX_FILES || tot + (long)len > CACHE_MAX_BYTES)) {
        int v = -1;
        for (int i = 0; i < n; i++)
            if (v < 0 || (L[i].used && !L[v].used) || (L[i].used == L[v].used && L[i].seq < L[v].seq)) v = i;
        char p[PATH_N + 32];
        snprintf(p, sizeof(p), CDIR "/%.23s", L[v].name);
        unlink(p);
        tot -= L[v].size;
        L[v] = L[--n];
    }
    char tmp[PATH_N], dst[PATH_N];
    snprintf(tmp, sizeof(tmp), CDIR "/.new.tmp");
    snprintf(dst, sizeof(dst), CDIR "/%08lx%c.json", (unsigned long)(mx + 1), dch);
    bool ok = write_atomic(tmp, dst, json);
    ESP_LOGD(TAG, "study cache + %08lx%c (%u B): %s", (unsigned long)(mx + 1), dch, (unsigned)len, ok ? "OK" : "LOI");
    cache_count();
}

/* used: .json -> .usd (da lay / da gui); !used: .usd -> .json (phien lay ra nhung chua lam cau nao: tra lai cache) */
static void cache_mark(const char *session_id, bool used)
{
    if (!session_id || !session_id[0] || !s_mx) return;
    cent_t L[CACHE_MAX_FILES + 8];
    int n = cache_list(L, CACHE_MAX_FILES + 8, NULL);
    char p[PATH_N + 32], q[PATH_N + 32], key[FD_STUDY_ID_LEN + 4];
    snprintf(key, sizeof(key), "\"%s\"", session_id);
    for (int i = 0; i < n; i++) {
        if (L[i].used == used) continue;
        snprintf(p, sizeof(p), CDIR "/%.23s", L[i].name);
        char *b = read_file(p, FILE_MAX, NULL);
        bool hit = b && strstr(b, key);
        free(b);
        if (!hit) continue;
        snprintf(q, sizeof(q), CDIR "/%.9s%s", L[i].name, used ? ".usd" : ".json");
        bool ok = rename(p, q) == 0;
        ESP_LOGD(TAG, "study cache: %.9s -> %s", L[i].name, ok ? (used ? "usd" : "json (tra lai)") : "LOI");
        break;
    }
    cache_count();
}

void fd_deck_cache_used(const char *session_id) { cache_mark(session_id, true); }
void fd_deck_cache_unuse(const char *session_id) { cache_mark(session_id, false); }

/* ---------- outbox ----------
 * /data/outbox/<session_id>.json = ket qua cho gui (ca phien online lan offline; session_id = khoa chong trung, server tra 409
 * neu da nhan). <session_id>.part = tien do dang lam (ghi sau MOI cau tra loi); luc khoi dong .part mo coi -> .json
 * (cau chua lam = ""). Gioi han OBOX_MAX file / OBOX_MAX_BYTES: tran thi bo ban CU nhat. */
static bool is_ext(const char *n, const char *ext)
{
    size_t l = strlen(n), k = strlen(ext);
    return l > k && strcmp(n + l - k, ext) == 0;
}

static int outbox_scan(char *oldest, size_t n, size_t *bytes)
{
    DIR *d = opendir(OBOX);
    if (bytes) *bytes = 0;
    if (!d) return 0;
    int cnt = 0;
    time_t old = 0;
    struct dirent *e;
    char p[PATH_N + 256];
    while ((e = readdir(d)) != NULL) {
        if (!is_ext(e->d_name, ".json")) continue;
        cnt++;
        snprintf(p, sizeof(p), OBOX "/%s", e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (bytes) *bytes += (size_t)st.st_size;
        if (oldest && (!oldest[0] || st.st_mtime < old)) { old = st.st_mtime; strlcpy(oldest, e->d_name, n); }
    }
    closedir(d);
    return cnt;
}

static void outbox_count(void)
{
    int n = outbox_scan(NULL, 0, NULL);
    lock();
    s_st.outbox = n;
    unlock();
}

static size_t clean_sid(const char *session_id, char *sid, size_t n)
{
    size_t k = 0;
    for (const char *p = session_id; p && *p && k < n - 1; p++)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') sid[k++] = *p;
    sid[k] = 0;
    return k;
}

static bool outbox_put(const char *session_id, const char *json, bool part)
{
    char sid[FD_STUDY_ID_LEN];
    if (!json || !clean_sid(session_id, sid, sizeof(sid))) return false;
    mkdir("/data", 0775);
    mkdir(OBOX, 0775);
    if (!part) {
        char oldest[64];
        size_t bytes;
        for (int guard = 0; guard < OBOX_MAX; guard++) {   /* tran so file / dung luong: bo ban cu nhat */
            oldest[0] = 0;
            int cnt = outbox_scan(oldest, sizeof(oldest), &bytes);
            if ((cnt < OBOX_MAX && bytes + strlen(json) <= OBOX_MAX_BYTES) || !oldest[0]) break;
            char p[PATH_N + 64];
            snprintf(p, sizeof(p), OBOX "/%s", oldest);
            unlink(p);
            ESP_LOGW(TAG, "outbox day (%d file, %u B): bo ban cu nhat %s", cnt, (unsigned)bytes, oldest);
        }
    }
    char tmp[PATH_N], dst[PATH_N];
    snprintf(tmp, sizeof(tmp), OBOX "/.%s.tmp", sid);
    snprintf(dst, sizeof(dst), OBOX "/%s%s", sid, part ? ".part" : ".json");
    bool ok = write_atomic(tmp, dst, json);
    if (ok && !part) {   /* ban day du da luu -> bo tien do */
        snprintf(dst, sizeof(dst), OBOX "/%s.part", sid);
        unlink(dst);
    }
    ESP_LOGD(TAG, "outbox %s %s: %s", part ? "tien do" : "+", sid, ok ? "OK" : "LOI");
    if (!part) outbox_count();
    return ok;
}

void fd_deck_outbox_add(const char *session_id, const char *json)
{
    if (!session_id || !json || !s_mx) return;
    outbox_put(session_id, json, false);
    if (s_q) { int c = CMD_SYNC; xQueueSend(s_q, &c, 0); }
}

bool fd_deck_outbox_save(const char *session_id, const char *json)
{
    return s_mx && outbox_put(session_id, json, false);
}

bool fd_deck_outbox_progress(const char *session_id, const char *json)
{
    return s_mx && outbox_put(session_id, json, true);
}

void fd_deck_outbox_remove(const char *session_id)
{
    char sid[FD_STUDY_ID_LEN], p[PATH_N];
    if (!s_mx || !clean_sid(session_id, sid, sizeof(sid))) return;
    snprintf(p, sizeof(p), OBOX "/%s.json", sid);
    bool had = unlink(p) == 0;
    snprintf(p, sizeof(p), OBOX "/%s.part", sid);
    had = (unlink(p) == 0) || had;
    if (had) ESP_LOGD(TAG, "outbox - %s (da gui)", sid);
    outbox_count();
}

/* Khoi dong: .part (phien dang lam do khi mat dien/khoi dong lai) -> .json de gui; .tmp do -> xoa. */
static void outbox_recover(void)
{
    DIR *d = opendir(OBOX);
    if (!d) return;
    struct dirent *e;
    char p[PATH_N + 256], q[PATH_N + 256];
    while ((e = readdir(d)) != NULL) {
        snprintf(p, sizeof(p), OBOX "/%s", e->d_name);
        if (is_ext(e->d_name, ".tmp")) { unlink(p); continue; }
        if (!is_ext(e->d_name, ".part")) continue;
        strlcpy(q, p, sizeof(q));
        strcpy(q + strlen(q) - 5, ".json");
        struct stat st;
        if (stat(q, &st) == 0) { unlink(p); continue; }   /* da co ban day du */
        bool ok = rename(p, q) == 0;
        ESP_LOGI(TAG, "outbox khoi phuc tien do %s: %s", e->d_name, ok ? "OK" : "LOI");
    }
    closedir(d);
}

/* Gui /api/study/result (fd_ota_api_post tu cho Retry-After khi 429, toi da 2 lan).
 * 2xx = xong -> xoa; 409 = server da nhan phien nay (trung) -> xoa; 400/404/410/422 = server tu choi vinh vien -> xoa + log;
 * 401 = fd_ota xoa token, ve Chua ghep, GIU outbox; 429 / 5xx / loi mang = giu, thu lai theo backoff.
 * Tra true neu khong con loi tam thoi. */
static bool outbox_flush(void)
{
    if (!fd_net_is_connected() || !fd_ota_is_paired()) return false;
    DIR *d = opendir(OBOX);
    if (!d) return true;
    struct dirent *e;
    char p[PATH_N + 256], rx[256];
    int done = 0;
    bool stop = false;
    while (!stop && (e = readdir(d)) != NULL) {
        if (!is_ext(e->d_name, ".json")) continue;
        snprintf(p, sizeof(p), OBOX "/%s", e->d_name);
        char *body = read_file(p, BODY_MAX, NULL);
        if (!body) { ESP_LOGW(TAG, "outbox %s: khong doc duoc / qua lon -> bo", e->d_name); unlink(p); continue; }
        int code = fd_ota_api_post("/api/study/result", body, rx, sizeof(rx));
        free(body);
        if (code >= 200 && code < 300) { unlink(p); done++; ESP_LOGI(TAG, "outbox gui %s -> %d: xong", e->d_name, code); }
        else if (code == 409) { unlink(p); done++; ESP_LOGI(TAG, "outbox gui %s -> 409: server da co (trung), xoa", e->d_name); }
        else if (code == 400 || code == 404 || code == 410 || code == 422) {
            unlink(p); done++;
            ESP_LOGW(TAG, "outbox gui %s -> %d: server tu choi, bo ban ghi", e->d_name, code);
        } else {
            ESP_LOGI(TAG, "outbox gui %s -> %d: giu, thu lai sau%s", e->d_name, code, code == 401 ? " (can ghep lai)" : "");
            stop = true;
        }
    }
    closedir(d);
    if (done || stop) outbox_count();
    if (done) ESP_LOGI(TAG, "outbox flush: gui %d, con %d", done, s_st.outbox);
    return !stop;
}

/* ---------- prefetch nen: POST /api/study/start (nguoi dung xac nhan: phai hoc offline duoc ngay ca khi chua hoc online) ---------- */
#define PF_TARGET_Q   100
#define PF_COUNT      20
#define PF_GAP_US     (15LL * 1000000)
#define PF_REFRESH_US (24LL * 3600 * 1000000)
#define PF_FRESH_MIN  2   /* giu toi thieu 2 phien chua dung moi chieu */

static int cache_q(void) { lock(); int q = s_st.words; unlock(); return q; }

/* true = thanh cong */
static bool prefetch_one(const char *dir)
{
    char id[16], body[160];
    fd_ota_get_device_id(id, sizeof(id));
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"direction\":\"%s\",\"count\":%d,\"source\":\"deck\"}", id, dir, PF_COUNT);
    char *rx = heap_caps_malloc(FILE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    fd_study_session_t *s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = false;
    int code = -1, n = 0;
    if (rx && s) {
        code = fd_ota_api_post("/api/study/start", body, rx, FILE_MAX);
        if (code == 200 && fd_study_parse_start(rx, s) == FD_STUDY_ERR_NONE) {
            n = s->n;
            fd_deck_cache_add(dir, rx);
            ok = true;
        }
    }
    ESP_LOGD(TAG, "study prefetch %s count=%d -> http=%d n=%d", dir, PF_COUNT, code, n);
    free(rx);
    free(s);
    return ok;
}

/* ---------- task ---------- */
static void task(void *arg)
{
    (void)arg;
    mkdir(CDIR, 0775);
    mkdir(OBOX, 0775);
    outbox_recover();
    cache_count();
    outbox_count();
    /* Prefetch: WiFi + da ghep, cache < PF_TARGET_Q cau hoac lan lam moi cuoi > 24 h (uptime) -> goi study/start
     * xen ke en-vi / vi-en, cach nhau PF_GAP_US; loi: backoff 60 s -> x2 -> toi da 30 phut. */
    bool was_on = false;
    int64_t last_flush = 0, next_pf = 0, backoff = 60LL * 1000000, last_full = 0;
    int64_t ob_next = 0, ob_back = OBOX_BACKOFF_MIN_US;   /* outbox: thu lai 30 s -> x2 -> toi da 30 phut */
    int dir_i = 0;
    int c;
    for (;;) {
        bool got = xQueueReceive(s_q, &c, pdMS_TO_TICKS(POLL_MS)) == pdTRUE;
        bool on = fd_net_is_connected() && fd_ota_is_paired();
        int64_t now = esp_timer_get_time();
        bool edge = on && !was_on;
        was_on = on;
        if (got && c == CMD_SCAN) cache_count();
        if (edge || (got && c == CMD_SYNC)) ob_next = 0;   /* vua co mang / co ket qua moi: gui ngay */
        if (on && s_st.outbox > 0 && (now >= ob_next || now - last_flush >= (int64_t)SYNC_MS * 1000 * 30)) {
            bool ok = outbox_flush();
            last_flush = now = esp_timer_get_time();
            if (ok) { ob_back = OBOX_BACKOFF_MIN_US; ob_next = now + (int64_t)SYNC_MS * 1000; }
            else {
                ob_next = now + ob_back;
                ESP_LOGI(TAG, "outbox: thu lai sau %d s", (int)(ob_back / 1000000));
                ob_back = ob_back * 2 > OBOX_BACKOFF_MAX_US ? OBOX_BACKOFF_MAX_US : ob_back * 2;
            }
        }
        if (got && c == CMD_REFILL) next_pf = 0;   /* vua lay 1 phien tu cache: bu lai ngay */
        bool stale = last_full == 0 || now - last_full >= PF_REFRESH_US;
        int fe = fresh_count('e'), fv = fresh_count('v');
        bool low = fe < PF_FRESH_MIN || fv < PF_FRESH_MIN;   /* cache-first can san phien chua dung cho ca 2 chieu */
        if (on && now >= next_pf && (cache_q() < PF_TARGET_Q || stale || low)) {
            if (low) dir_i = fe <= fv ? 0 : 1;   /* bu chieu dang thieu truoc */
            bool ok = prefetch_one(dir_i ? "vi-en" : "en-vi");
            dir_i ^= 1;
            now = esp_timer_get_time();
            if (ok) {
                backoff = 60LL * 1000000;
                next_pf = now + PF_GAP_US;
                if (cache_q() >= PF_TARGET_Q && dir_i == 0 && !low) { last_full = now; ESP_LOGI(TAG, "study prefetch du: %d cau", cache_q()); }
            } else {
                next_pf = now + backoff;
                backoff = backoff * 2 > 30LL * 60 * 1000000 ? 30LL * 60 * 1000000 : backoff * 2;
            }
        }
    }
}

void fd_deck_init(void)
{
    if (s_q) return;
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    QueueHandle_t q = xQueueCreate(4, sizeof(int));
    if (!s_mx || !q) { ESP_LOGI(TAG, "init: het bo nho"); return; }
    s_q = q;
    if (xTaskCreate(task, "fd_deck", 8192, NULL, 2, NULL) != pdPASS) ESP_LOGI(TAG, "init: khong tao duoc task");
}

void fd_deck_download(void)   /* khong con tai bo tu: chi quet lai cache + gui outbox */
{
    fd_deck_init();
    if (!s_q) return;
    int c = CMD_SCAN;
    xQueueSend(s_q, &c, 0);
    c = CMD_SYNC;
    xQueueSend(s_q, &c, 0);
}

void fd_deck_get(fd_deck_status_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    lock();
    *out = s_st;
    unlock();
}

bool fd_deck_ready(void)
{
    if (!s_mx) return false;
    lock();
    bool h = s_st.have;
    unlock();
    return h;
}

/* ---------- phien offline tu cache ---------- */
fd_study_err_t fd_deck_build_session(fd_study_session_t *s, bool vi_en, bool fresh_only)
{
    if (!s) return FD_STUDY_ERR_FORMAT;
    memset(s, 0, sizeof(*s));
    if (!s_mx) return FD_STUDY_ERR_NO_DECK;
    cent_t L[CACHE_MAX_FILES + 8];
    int m = cache_list(L, CACHE_MAX_FILES + 8, NULL);
    char want = vi_en ? 'v' : 'e';
    /* uu tien: dung chieu + chua gui > dung chieu da gui > chieu khac chua gui > con lai; ngau nhien trong nhom */
    int best = -1, rank = 99, ties = 0;
    for (int i = 0; i < m; i++) {
        int r = (L[i].dir == want ? 0 : 2) + (L[i].used ? 1 : 0);
        if (r < rank) { rank = r; best = i; ties = 1; }
        else if (r == rank && esp_random() % (uint32_t)(++ties) == 0) best = i;
    }
    fd_study_err_t err = FD_STUDY_ERR_NO_DECK;
    if (fresh_only && rank != 0) best = -1;   /* chi phien dung chieu chua dung */
    if (best >= 0) {
        char p[PATH_N + 32];
        snprintf(p, sizeof(p), CDIR "/%.23s", L[best].name);
        char *b = read_file(p, FILE_MAX, NULL);
        err = b ? fd_study_parse_start(b, s) : FD_STUDY_ERR_NO_DECK;
        free(b);
        if (err) { unlink(p); cache_count(); }   /* hong: bo */
    }
    if (!err) {
        for (int i = s->n - 1; i > 0; i--) {   /* tron thu tu cau (giu nguyen key A-D cua tung cau) */
            int j = (int)(esp_random() % (uint32_t)(i + 1));
            fd_study_q_t t = s->q[i]; s->q[i] = s->q[j]; s->q[j] = t;
        }
        s->offline = true;
    }
    ESP_LOGI(TAG, "phien offline tu cache %s: %d cau err=%d", best >= 0 ? L[best].name : "-", s->n, err);
    return err;
}

bool fd_deck_find_audio(const char *word, char *path, size_t plen, uint32_t *off, uint32_t *len)
{
    (void)word; (void)path; (void)plen; (void)off; (void)len;
    return false;   /* khong con audio.pak: fd_audio dung /data/voice -> Pico */
}
