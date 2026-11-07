/* Dong bo cloud <-> thiet bi: xem fd_sync.h. KHONG log token / Authorization / body. */
#include "fd_sync.h"
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
#include "esp_timer.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "fd_sync";

#define SDIR        "/data/sync"
#define STORE       SDIR "/store.tsv"
#define STORE_TMP   SDIR "/store.tmp"
#define EVLOG       SDIR "/events.log"
#define EVLOG_TMP   SDIR "/events.tmp"
#define VLOG        SDIR "/card_seen.log"
#define VLOG_TMP    SDIR "/card_seen.tmp"
#define STORE_MAX_BYTES (3 * 1024 * 1024)
#define EV_MAX_BYTES    (512 * 1024)
#define PUSH_BATCH  50
#define PUSH_ROUNDS 20
#define PAGE        40
#define RX_CAP      (128 * 1024)
#define LINE_CAP    (12 * 1024)
#define CHECK_US    (90LL * 1000000)       /* kiem tra rev moi 90 s khi co mang */
#define BACK_MIN_US (60LL * 1000000)
#define BACK_MAX_US (15LL * 60 * 1000000)
#define MASTER_STREAK 3
#define NVS_NS      "fdsync"

enum { CMD_PUSH = 1, CMD_FORCE, CMD_STATS };

typedef struct {
    char id[FD_SYNC_WID_LEN], cid[FD_SYNC_WID_LEN];
    uint8_t st, nsen, streak;
    uint16_t seen, views;
    uint32_t ca, off;
    char en[FD_FC_WORD_LEN], ipa[FD_FC_IPA_LEN], vi[FD_FC_MEAN_LEN];
    char pos[24];   /* 2.3.5: loai tu (noun/verb/...) tu store.tsv */
} word_t;

static SemaphoreHandle_t s_mx;    /* kho (s_w, file store.tsv) + s_st */
static SemaphoreHandle_t s_evmx;  /* events.log */
static QueueHandle_t s_q;
static word_t *s_w;               /* FD_SYNC_MAX_WORDS, PSRAM */
static int s_nw;
static char s_rev[32];
static fd_sync_status_t s_st;
static fd_sync_stats_t s_stats;

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

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

static bool is_wid(const char *s)
{
    if (!s || strlen(s) != 24) return false;
    for (int i = 0; i < 24; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static time_t now_epoch(void) { return fd_net_time_valid() ? time(NULL) : 0; }

static void set_err(const char *e)
{
    lock();
    strlcpy(s_st.err, e ? e : "", sizeof(s_st.err));
    s_st.state = (e && e[0]) ? FD_SYNC_ERROR : FD_SYNC_OK;
    unlock();
}

static void code_err(int code, char *out, size_t n)
{
    if (code == 401) strlcpy(out, "unpaired", n);
    else if (code == 403) strlcpy(out, "no_account", n);
    else if (code == 429) strlcpy(out, "rate_limited", n);
    else if (code < 0) strlcpy(out, "net", n);
    else snprintf(out, n, "http_%d", code);
}

/* ---------- NVS: cai dat nho + lan dong bo cuoi ---------- */
static void nvs_save_settings(bool pn, bool pu, const char *topics)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "p_new", pn);
    nvs_set_u8(h, "p_unl", pu);
    nvs_set_str(h, "topics", topics ? topics : "");
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_last(time_t t)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i64(h, "last", (int64_t)t);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    if (nvs_get_u8(h, "p_new", &v) == ESP_OK) s_st.prio_new = v;
    if (nvs_get_u8(h, "p_unl", &v) == ESP_OK) s_st.prio_unl = v;
    int64_t t = 0;
    if (nvs_get_i64(h, "last", &t) == ESP_OK) s_st.last_sync = (time_t)t;
    nvs_close(h);
}

/* ---------- store.tsv ---------- */
static int split_tabs(char *line, char **f, int cap)
{
    int n = 0;
    char *p = line;
    while (n < cap) {
        f[n++] = p;
        char *t = strchr(p, '\t');
        if (!t) break;
        *t = 0;
        p = t + 1;
    }
    return n;
}

static int topics_count(const char *csv)
{
    if (!csv || !csv[0]) return 0;
    int n = 1;
    for (const char *p = csv; *p; p++) n += *p == ',';
    return n;
}

/* Tien do cuc bo (chua gui) ap len trang thai tu: cung quy tac server (3 lan dung lien tiep = thuoc). */
static void apply_review(word_t *w, bool ok)
{
    if (ok) {
        if (w->streak < 255) w->streak++;
        if (w->streak >= MASTER_STREAK) w->st = 2;
        else if (w->st == 0) w->st = 1;
    } else {
        w->streak = 0;
        if (w->st != 1) w->st = 1;
    }
}

static word_t *find_word(const char *id)
{
    for (int i = 0; i < s_nw; i++) if (!strcmp(s_w[i].id, id)) return &s_w[i];
    return NULL;
}

static void replay_events(void);

/* Doc store.tsv vao RAM (giu khoa). false = khong co / hong (kho rong). */
static void views_replay_locked(void);

static bool load_store_locked(void)
{
    s_nw = 0;
    s_rev[0] = 0;
    s_st.have = false;
    s_st.words = s_st.sentences = s_st.collections = 0;
    FILE *f = fopen(STORE, "r");
    if (!f) return false;
    char *line = ps_alloc(LINE_CAP);
    if (!line) { fclose(f); return false; }
    char *fl[32];
    bool head = false, end = false, bad = false;
    int nc = 0, ns = 0;
    bool pn = false, pu = false;
    char topics[160] = "";
    for (;;) {
        long off = ftell(f);
        if (!fgets(line, LINE_CAP, f)) break;
        size_t L = strlen(line);
        if (!L || line[L - 1] != '\n') { bad = true; break; }   /* dong cut (mat dien khi ghi) / qua dai */
        line[--L] = 0;
        if (end) { bad = true; break; }
        int n = split_tabs(line, fl, 32);
        if (!head) {
            if (n < 3 || strcmp(fl[0], "#FDS1")) { bad = true; break; }
            strlcpy(s_rev, fl[1], sizeof(s_rev));
            head = true;
        } else if (!strcmp(fl[0], "S") && n >= 4) {
            pn = fl[1][0] == '1'; pu = fl[2][0] == '1';
            strlcpy(topics, fl[3], sizeof(topics));
        } else if (!strcmp(fl[0], "C")) {
            nc++;
        } else if (!strcmp(fl[0], "W") && n >= 11) {
            if (s_nw >= FD_SYNC_MAX_WORDS) continue;
            word_t *w = &s_w[s_nw];
            memset(w, 0, sizeof(*w));
            ucpy(w->id, sizeof(w->id), fl[1]);
            ucpy(w->cid, sizeof(w->cid), fl[2]);
            w->st = (uint8_t)atoi(fl[3]);
            if (w->st > 2) w->st = 0;
            w->ca = (uint32_t)strtoul(fl[4], NULL, 10);
            ucpy(w->en, sizeof(w->en), fl[5]);
            ucpy(w->ipa, sizeof(w->ipa), fl[6]);
            ucpy(w->vi, sizeof(w->vi), fl[7]);
            ucpy(w->pos, sizeof(w->pos), fl[8]);
            int k = atoi(fl[10]);
            if (k < 0) k = 0;
            if (k > (n - 11) / 2) k = (n - 11) / 2;
            w->nsen = (uint8_t)k;
            w->off = (uint32_t)off;
            if (!w->en[0] || !w->vi[0] || !is_wid(w->id)) continue;
            ns += k;
            s_nw++;
        } else if (!strcmp(fl[0], "#END") && n >= 4) {
            end = atoi(fl[1]) == s_nw && atoi(fl[2]) == nc;
            if (!end) { bad = true; break; }
        }
    }
    free(line);
    fclose(f);
    if (bad || !end) {
        ESP_LOGW(TAG, "store.tsv hong/thieu -> bo, se tai lai toan bo");
        s_nw = 0;
        s_rev[0] = 0;
        unlink(STORE);
        return false;
    }
    s_st.have = true;
    s_st.words = s_nw;
    s_st.sentences = ns;
    s_st.collections = nc;
    s_st.prio_new = pn;
    s_st.prio_unl = pu;
    s_st.topics = topics_count(topics);
    views_replay_locked();
    replay_events();
    ESP_LOGI(TAG, "kho: %d tu, %d cau, %d bo", s_nw, ns, nc);
    return true;
}

/* ---------- outbox events.log ---------- */
static char *read_all(const char *p, size_t max, size_t *len)
{
    *len = 0;
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = NULL;
    if (n >= 0 && (size_t)n <= max) {
        b = ps_alloc((size_t)n + 1);
        if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
        if (b) { b[n] = 0; *len = (size_t)n; }
    }
    fclose(f);
    return b;
}

static bool line_ok(const char *s, size_t n) { return n > 10 && s[0] == '{' && s[n - 1] == '}'; }

/* eid trong dong JSON -> out */
static bool line_eid(const char *s, size_t n, char *out, size_t cap)
{
    const char *k = "\"eid\":\"";
    const char *p = NULL;
    for (size_t i = 0; i + 7 < n; i++) if (!memcmp(s + i, k, 7)) { p = s + i + 7; break; }
    if (!p) return false;
    size_t m = 0;
    while (p + m < s + n && p[m] != '"' && m + 1 < cap) m++;
    memcpy(out, p, m);
    out[m] = 0;
    return m > 0;
}

static int count_events(void)
{
    size_t len;
    char *b = read_all(EVLOG, EV_MAX_BYTES * 2, &len);
    if (!b) return 0;
    int n = 0;
    for (size_t i = 0, s = 0; i < len; i++) {
        if (b[i] != '\n') continue;
        n += line_ok(b + s, i - s);
        s = i + 1;
    }
    free(b);
    return n;
}

static bool write_atomic_buf(const char *tmp, const char *dst, const char *data, size_t n)
{
    FILE *f = fopen(tmp, "wb");
    bool ok = f && (n == 0 || fwrite(data, 1, n, f) == n);
    if (f) {
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;
        ok = fclose(f) == 0 && ok;
    }
    ok = ok && rename(tmp, dst) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

/* Ghi lai events.log bo cac dong co eid trong acked (+ dong hong). skip_first: bo them N dong dau (tran). Giu s_evmx. */
static void ev_rewrite(const char *acked, int skip_first)
{
    size_t len;
    char *b = read_all(EVLOG, EV_MAX_BYTES * 2, &len);
    if (!b) return;
    char *o = ps_alloc(len + 1);
    if (!o) { free(b); return; }
    size_t on = 0;
    int idx = 0;
    char eid[48], key[52];
    for (size_t i = 0, s = 0; i < len; i++) {
        if (b[i] != '\n') continue;
        size_t n = i - s;
        bool keep = line_ok(b + s, n) && idx++ >= skip_first;
        if (keep && acked && line_eid(b + s, n, eid, sizeof(eid))) {
            snprintf(key, sizeof(key), "\"%s\"", eid);
            if (strstr(acked, key)) keep = false;
        }
        if (keep) { memcpy(o + on, b + s, n + 1); on += n + 1; }
        s = i + 1;
    }
    if (!write_atomic_buf(EVLOG_TMP, EVLOG, o, on)) ESP_LOGW(TAG, "khong ghi lai duoc events.log");
    free(o);
    free(b);
}

static void replay_events(void)   /* giu s_mx */
{
    if (!s_evmx) return;
    xSemaphoreTake(s_evmx, portMAX_DELAY);
    size_t len;
    char *b = read_all(EVLOG, EV_MAX_BYTES * 2, &len);
    xSemaphoreGive(s_evmx);
    if (!b) return;
    for (size_t i = 0, s = 0; i < len; i++) {
        if (b[i] != '\n') continue;
        b[i] = 0;
        char *p = strstr(b + s, "\"wid\":\"");
        if (p && line_ok(b + s, i - s)) {
            char id[FD_SYNC_WID_LEN];
            ucpy(id, sizeof(id), p + 7);
            id[24] = 0;
            word_t *w = find_word(id);
            if (w) apply_review(w, strstr(b + s, "\"ok\":1") != NULL);
        }
        s = i + 1;
    }
    free(b);
}

void fd_sync_event(const char *kind, const char *wid, bool correct)
{
    if (!s_mx || !s_evmx || !is_wid(wid)) return;
    char line[160];
    snprintf(line, sizeof(line), "{\"eid\":\"e%08lx%08lx\",\"k\":\"%s\",\"wid\":\"%s\",\"ok\":%d,\"ts\":%lld}\n",
             (unsigned long)esp_random(), (unsigned long)esp_random(),
             (kind && (!strcmp(kind, "card") || !strcmp(kind, "sen"))) ? kind : "quiz", wid, correct ? 1 : 0,
             (long long)now_epoch());
    xSemaphoreTake(s_evmx, portMAX_DELAY);
    mkdir(SDIR, 0775);
    struct stat st;
    if (stat(EVLOG, &st) == 0 && st.st_size > EV_MAX_BYTES) {
        ESP_LOGW(TAG, "outbox day -> bo 500 ket qua cu nhat");
        ev_rewrite(NULL, 500);
    }
    FILE *f = fopen(EVLOG, "a");
    bool ok = f && fputs(line, f) >= 0;
    if (f) {
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;   /* ben vung qua mat dien */
        ok = fclose(f) == 0 && ok;
    }
    xSemaphoreGive(s_evmx);
    lock();
    if (ok) s_st.pending++;
    word_t *w = find_word(wid);
    if (w) apply_review(w, correct);
    unlock();
    if (!ok) ESP_LOGW(TAG, "khong ghi duoc ket qua vao outbox");
}

/* Gui outbox theo lo; chi xoa dong server da ack. Tra HTTP cuoi (200 = sach / khong con gi), <0 mang. */
static int push_all(char *rx)
{
    int code = 200;
    char id[16];
    fd_ota_get_device_id(id, sizeof(id));
    for (int round = 0; round < PUSH_ROUNDS; round++) {
        xSemaphoreTake(s_evmx, portMAX_DELAY);
        size_t len;
        char *b = read_all(EVLOG, EV_MAX_BYTES * 2, &len);
        xSemaphoreGive(s_evmx);
        if (!b) break;
        size_t cap = 64 + len < 64 + PUSH_BATCH * 200 ? 64 + len : 64 + PUSH_BATCH * 200;
        char *body = ps_alloc(cap + 64);
        int n = 0;
        size_t bn = 0;
        if (body) {
            bn = (size_t)snprintf(body, cap, "{\"device_id\":\"%s\",\"events\":[", id);
            for (size_t i = 0, s = 0; i < len && n < PUSH_BATCH; i++) {
                if (b[i] != '\n') continue;
                size_t m = i - s;
                if (line_ok(b + s, m) && bn + m + 4 < cap) {
                    if (n) body[bn++] = ',';
                    memcpy(body + bn, b + s, m);
                    bn += m;
                    n++;
                }
                s = i + 1;
            }
            memcpy(body + bn, "]}", 3);
        }
        free(b);
        if (!body) { code = -2; break; }
        if (n == 0) { free(body); break; }
        lock(); s_st.state = FD_SYNC_PUSHING; unlock();
        code = fd_ota_api_post("/api/study/sync/push", body, rx, RX_CAP);
        free(body);
        if (code != 200) { ESP_LOGI(TAG, "push %d ket qua -> %d: giu, thu lai sau", n, code); break; }
        cJSON *r = cJSON_Parse(rx);
        const cJSON *ak = r ? cJSON_GetObjectItemCaseSensitive(r, "acked") : NULL;
        char *acked = ak ? cJSON_PrintUnformatted(ak) : NULL;   /* ["e..","e.."] -> tim "eid" */
        cJSON_Delete(r);
        if (!acked) { code = -3; break; }
        xSemaphoreTake(s_evmx, portMAX_DELAY);
        ev_rewrite(acked, 0);
        xSemaphoreGive(s_evmx);
        free(acked);
        int left = count_events();
        lock(); s_st.pending = left; unlock();
        ESP_LOGI(TAG, "push %d ket qua -> ack, con %d", n, left);
        if (left == 0) break;
    }
    return code;
}

/* ---------- pull ---------- */
static void clean_field(char *s)   /* tab / xuong dong trong du lieu -> dau cach (dinh dang TSV) */
{
    for (; s && *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

static void put_field(FILE *f, const char *s, size_t max, char *tmp)
{
    ucpy(tmp, max, s ? s : "");
    clean_field(tmp);
    fputc('\t', f);
    fputs(tmp, f);
}

static const char *js(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static int jint(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

/* Tai snapshot day du (moi trang kiem tra rev khong doi). Tra 0 OK, HTTP/-1 loi; err dien ma. */
static int pull_all(char *rx, char *err, size_t errn)
{
    char id[16], body[160], tmp[512];
    fd_ota_get_device_id(id, sizeof(id));
    mkdir(SDIR, 0775);
    for (int attempt = 0; attempt < 3; attempt++) {
        FILE *f = NULL;
        char rev[32] = "";
        int offset = 0, total = 0, nw = 0, nc = 0, ns = 0, code = 200;
        long bytes = 0;
        bool restart = false, fail = false;
        for (;;) {
            snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"offset\":%d,\"limit\":%d}", id, offset, PAGE);
            code = fd_ota_api_post("/api/study/sync/pull", body, rx, RX_CAP);
            if (code != 200) { code_err(code, err, errn); fail = true; break; }
            cJSON *r = cJSON_Parse(rx);
            if (!r) { strlcpy(err, "format", errn); fail = true; break; }
            const char *rv = js(r, "rev");
            if (!rv[0] || (rev[0] && strcmp(rev, rv))) {   /* du lieu doi giua chung: lam lai */
                cJSON_Delete(r);
                restart = rev[0] != 0;
                if (!restart) strlcpy(err, "format", errn);
                fail = !restart;
                break;
            }
            if (!f) {
                strlcpy(rev, rv, sizeof(rev));
                total = jint(r, "total");
                f = fopen(STORE_TMP, "w");
                if (!f) { cJSON_Delete(r); strlcpy(err, "fs", errn); fail = true; break; }
                fprintf(f, "#FDS1\t%s\t%lld\n", rev, (long long)now_epoch());
                const cJSON *st = cJSON_GetObjectItemCaseSensitive(r, "settings");
                const cJSON *tp = st ? cJSON_GetObjectItemCaseSensitive(st, "topics") : NULL;
                char topics[160] = "";
                const cJSON *t;
                cJSON_ArrayForEach(t, tp) {
                    if (!cJSON_IsString(t)) continue;
                    if (topics[0]) strlcat(topics, ",", sizeof(topics));
                    strlcat(topics, t->valuestring, sizeof(topics));
                }
                clean_field(topics);
                bool pn = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(st, "priority_new"));
                bool pu = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(st, "priority_unlearned"));
                fprintf(f, "S\t%d\t%d\t%s\n", pn, pu, topics);
                nvs_save_settings(pn, pu, topics);
                const cJSON *c;
                cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(r, "collections")) {
                    fputs("C", f);
                    put_field(f, js(c, "id"), FD_SYNC_WID_LEN, tmp);
                    fprintf(f, "\t%d", cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(c, "def")));
                    put_field(f, js(c, "name"), 192, tmp);
                    fputc('\n', f);
                    nc++;
                }
            }
            const cJSON *w;
            cJSON_ArrayForEach(w, cJSON_GetObjectItemCaseSensitive(r, "words")) {
                if (nw >= FD_SYNC_MAX_WORDS) break;
                fputs("W", f);
                put_field(f, js(w, "id"), FD_SYNC_WID_LEN, tmp);
                put_field(f, js(w, "c"), FD_SYNC_WID_LEN, tmp);
                fprintf(f, "\t%d\t%d", jint(w, "st"), jint(w, "ca"));
                put_field(f, js(w, "en"), FD_FC_WORD_LEN, tmp);
                put_field(f, js(w, "ipa"), FD_FC_IPA_LEN, tmp);
                put_field(f, js(w, "vi"), FD_FC_MEAN_LEN, tmp);
                put_field(f, js(w, "pos"), 96, tmp);
                put_field(f, js(w, "ex"), 480, tmp);
                const cJSON *sa = cJSON_GetObjectItemCaseSensitive(w, "s");
                int k = 0;
                const cJSON *p;
                cJSON_ArrayForEach(p, sa) if (cJSON_IsArray(p) && cJSON_GetArraySize(p) >= 1 && k < 8) k++;
                fprintf(f, "\t%d", k);
                int j = 0;
                cJSON_ArrayForEach(p, sa) {
                    if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) < 1 || j >= k) continue;
                    const cJSON *e0 = cJSON_GetArrayItem(p, 0), *e1 = cJSON_GetArrayItem(p, 1);
                    put_field(f, cJSON_IsString(e0) ? e0->valuestring : "", 480, tmp);
                    put_field(f, cJSON_IsString(e1) ? e1->valuestring : "", 480, tmp);
                    j++;
                }
                fputc('\n', f);
                nw++;
                ns += k;
            }
            const cJSON *nx = cJSON_GetObjectItemCaseSensitive(r, "next_offset");
            int next = cJSON_IsNumber(nx) ? nx->valueint : -1;
            cJSON_Delete(r);
            bytes = ftell(f);
            if (bytes > STORE_MAX_BYTES) { strlcpy(err, "too_large", errn); fail = true; break; }
            lock();
            s_st.state = FD_SYNC_PULLING;
            s_st.pct = total > 0 ? (nw * 100 / total) : 100;
            unlock();
            if (next <= offset) break;
            offset = next;
        }
        if (f && !fail && !restart) {
            fprintf(f, "#END\t%d\t%d\t%d\n", nw, nc, ns);
            bool ok = fflush(f) == 0;
            ok = fsync(fileno(f)) == 0 && ok;
            ok = fclose(f) == 0 && ok;
            f = NULL;
            lock();
            ok = ok && rename(STORE_TMP, STORE) == 0;   /* nguyen tu: ban cu hoac ban moi */
            if (ok) load_store_locked();
            unlock();
            if (!ok) { unlink(STORE_TMP); strlcpy(err, "fs", errn); return -1; }
            ESP_LOGI(TAG, "tai xong: %d tu, %d bo, %d cau, %ld B", nw, nc, ns, bytes);
            return 0;
        }
        if (f) fclose(f);
        unlink(STORE_TMP);
        if (fail) return code == 200 ? -1 : code;
        ESP_LOGI(TAG, "du lieu doi khi dang tai -> tai lai (%d)", attempt + 1);
    }
    strlcpy(err, "busy", errn);
    return -1;
}

/* Dong bo 1 lan: push outbox -> rev -> pull neu doi (force: luon pull). true = thanh cong. */
static bool do_sync(char *rx, bool force)
{
    char err[24] = "";
    if (!fd_ota_is_paired()) { set_err("unpaired"); return false; }
    if (!fd_net_is_connected()) { set_err("no_wifi"); return false; }
    lock(); s_st.state = FD_SYNC_CHECKING; s_st.pct = 0; unlock();
    int code = push_all(rx);
    if (code != 200) {
        code_err(code, err, sizeof(err));
        if (code == 401) { set_err(err); return false; }   /* fd_ota da xoa token: ve Chua ghep, giu du lieu */
    }
    bool need = force;
    if (!need) {
        char id[16], body[64];
        fd_ota_get_device_id(id, sizeof(id));
        snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", id);
        int c = fd_ota_api_post("/api/study/sync/rev", body, rx, RX_CAP);
        if (c != 200) { code_err(c, err, sizeof(err)); set_err(err); return false; }
        cJSON *r = cJSON_Parse(rx);
        const char *rv = r ? js(r, "rev") : "";
        lock();
        need = !rv[0] || !s_st.have || strcmp(rv, s_rev) != 0;
        unlock();
        cJSON_Delete(r);
    }
    if (need) {
        int c = pull_all(rx, err, sizeof(err));
        if (c != 0) { set_err(err); return false; }
    }
    time_t t = now_epoch();
    lock();
    if (t) s_st.last_sync = t;
    s_st.pct = 100;
    unlock();
    if (t) nvs_save_last(t);
    set_err(code == 200 ? "" : err);   /* outbox loi tam thoi: van bao loi de nguoi dung thay */
    return code == 200;
}

static void do_stats(char *rx)
{
    lock();
    s_stats.state = FD_STATS_LOADING;
    s_stats.err[0] = 0;
    unlock();
    char err[24] = "";
    if (!fd_ota_is_paired()) strlcpy(err, "unpaired", sizeof(err));
    else if (!fd_net_is_connected()) strlcpy(err, "no_wifi", sizeof(err));
    else {
        push_all(rx);   /* gui ket qua offline truoc de thong ke moi nhat */
        char id[16], body[64];
        fd_ota_get_device_id(id, sizeof(id));
        snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", id);
        int c = fd_ota_api_post("/api/study/stats", body, rx, RX_CAP);
        cJSON *r = c == 200 ? cJSON_Parse(rx) : NULL;
        if (c != 200) code_err(c, err, sizeof(err));
        else if (!r) strlcpy(err, "format", sizeof(err));
        else {
            const cJSON *td = cJSON_GetObjectItemCaseSensitive(r, "today"), *wk = cJSON_GetObjectItemCaseSensitive(r, "week");
            lock();
            s_stats.words = jint(r, "words");
            s_stats.n_new = jint(r, "new");
            s_stats.learning = jint(r, "learning");
            s_stats.mastered = jint(r, "mastered");
            s_stats.collections = jint(r, "collections");
            s_stats.sentences = jint(r, "sentences");
            s_stats.today_answers = jint(td, "answers");
            s_stats.today_correct = jint(td, "correct");
            s_stats.week_answers = jint(wk, "answers");
            s_stats.week_correct = jint(wk, "correct");
            s_stats.week_sessions = jint(wk, "sessions");
            s_stats.streak_days = jint(r, "streak_days");
            unlock();
        }
        cJSON_Delete(r);
    }
    lock();
    strlcpy(s_stats.err, err, sizeof(s_stats.err));
    s_stats.state = err[0] ? FD_STATS_ERROR : FD_STATS_OK;
    unlock();
    ESP_LOGI(TAG, "thong ke: %s", err[0] ? err : "OK");
}

static void task(void *arg)
{
    (void)arg;
    char *rx = ps_alloc(RX_CAP);
    bool was_on = false;
    int64_t next = 0, back = BACK_MIN_US;
    for (;;) {
        int c = 0;
        bool got = xQueueReceive(s_q, &c, pdMS_TO_TICKS(2000)) == pdTRUE;
        if (!rx) { vTaskDelay(pdMS_TO_TICKS(10000)); rx = ps_alloc(RX_CAP); continue; }
        bool on = fd_net_is_connected() && fd_ota_is_paired();
        int64_t now = esp_timer_get_time();
        if (on && !was_on) next = 0;   /* vua co mang: dong bo ngay */
        was_on = on;
        if (got && c == CMD_STATS) { do_stats(rx); continue; }
        if (got && c == CMD_FORCE) {
            lock(); s_st.manual = true; unlock();
            bool ok = do_sync(rx, true);
            lock(); s_st.manual = false; unlock();
            back = BACK_MIN_US;
            next = esp_timer_get_time() + (ok ? CHECK_US : back);
            continue;
        }
        if (!on) continue;
        lock(); int pend = s_st.pending; unlock();
        if (got && c == CMD_PUSH && pend > 0) {
            int code = push_all(rx);
            char e[24] = "";
            if (code != 200) code_err(code, e, sizeof(e));
            set_err(e);
            continue;
        }
        if (now >= next) {
            bool ok = do_sync(rx, false);
            now = esp_timer_get_time();
            if (ok) { back = BACK_MIN_US; next = now + CHECK_US; }
            else { next = now + back; back = back * 2 > BACK_MAX_US ? BACK_MAX_US : back * 2; }
        }
    }
}

void fd_sync_init(void)
{
    if (s_q) return;
    s_mx = xSemaphoreCreateMutex();
    s_evmx = xSemaphoreCreateMutex();
    s_w = heap_caps_calloc(FD_SYNC_MAX_WORDS, sizeof(word_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_q = xQueueCreate(4, sizeof(int));
    if (!s_mx || !s_evmx || !s_w || !s_q) {
        if (s_q) vQueueDelete(s_q);
        if (s_mx) vSemaphoreDelete(s_mx);
        if (s_evmx) vSemaphoreDelete(s_evmx);
        heap_caps_free(s_w);
        s_q = NULL; s_mx = NULL; s_evmx = NULL; s_w = NULL;
        ESP_LOGE(TAG, "init: het bo nho"); return;
    }
    mkdir(SDIR, 0775);
    unlink(STORE_TMP);   /* ban tai do (mat dien giua chung) */
    unlink(EVLOG_TMP);
    lock();
    nvs_load();
    load_store_locked();
    unlock();
    int p = count_events();
    lock(); s_st.pending = p; unlock();
    if (xTaskCreate(task, "fd_sync", 8192, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(s_q); vSemaphoreDelete(s_mx); vSemaphoreDelete(s_evmx); heap_caps_free(s_w);
        s_q = NULL; s_mx = NULL; s_evmx = NULL; s_w = NULL;
        ESP_LOGE(TAG, "init: khong tao duoc task");
    }
}

static void send_cmd(int c)
{
    if (s_q) xQueueSend(s_q, &c, 0);
}

void fd_sync_kick(void) { send_cmd(CMD_PUSH); }
void fd_sync_force(void) { send_cmd(CMD_FORCE); }

void fd_sync_stats_fetch(void)
{
    if (!s_q) return;
    lock(); s_stats.state = FD_STATS_LOADING; unlock();
    send_cmd(CMD_STATS);
}

void fd_sync_get(fd_sync_status_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    lock(); *out = s_st; unlock();
}

void fd_sync_stats_get(fd_sync_stats_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    lock(); *out = s_stats; unlock();
}

bool fd_sync_ready(void)
{
    if (!s_mx) return false;
    lock(); bool r = s_st.have && s_nw >= 4; unlock();
    return r;
}

/* ---------- tao bai tu kho (theo uu tien hoc) ---------- */
typedef struct { uint64_t key; int idx; } ord_t;

static int ord_cmp(const void *a, const void *b)
{
    uint64_t x = ((const ord_t *)a)->key, y = ((const ord_t *)b)->key;
    return x < y ? -1 : x > y;
}

/* Thu tu hoc (giu khoa): [chua thuoc truoc neu bat] > [it gap trong lan chay nay] > [moi them truoc neu bat | ngau nhien].
 * filt: chi lay tu co cau vi du. Tra so phan tu (cap). */
static int order_words(ord_t *o, int cap, bool filt)
{
    int n = 0;
    for (int i = 0; i < s_nw; i++) {
        if (filt && s_w[i].nsen == 0) continue;
        uint64_t unl = (s_st.prio_unl && s_w[i].st == 2) ? 1 : 0;
        uint64_t seen = s_w[i].seen > 255 ? 255 : s_w[i].seen;
        uint64_t sub = s_st.prio_new ? (uint64_t)(0xFFFFFFFFu - s_w[i].ca) : (uint64_t)esp_random();
        o[n].key = (unl << 56) | (seen << 48) | ((sub & 0xFFFFFFFFull) << 16) | (esp_random() & 0xFFFF);
        o[n].idx = i;
        n++;
    }
    qsort(o, n, sizeof(*o), ord_cmp);
    return n < cap ? n : cap;
}

static int rnd(int n) { return n > 1 ? (int)(esp_random() % (uint32_t)n) : 0; }

fd_study_err_t fd_sync_build_quiz(fd_study_session_t *s, bool vi_en, int n)
{
    if (!s) return FD_STUDY_ERR_FORMAT;
    memset(s, 0, sizeof(*s));
    if (!fd_sync_ready()) return FD_STUDY_ERR_NO_DECK;
    if (n <= 0 || n > FD_STUDY_MAX) n = FD_STUDY_MAX;
    ord_t *o = ps_alloc(sizeof(ord_t) * FD_SYNC_MAX_WORDS);
    if (!o) return FD_STUDY_ERR_NOMEM;
    lock();
    int m = order_words(o, n, false);
    for (int q = 0; q < m; q++) {
        word_t *w = &s_w[o[q].idx];
        fd_study_q_t *d = &s->q[s->n];
        ucpy(d->id, sizeof(d->id), w->id);
        ucpy(d->prompt, sizeof(d->prompt), vi_en ? w->vi : w->en);
        if (!vi_en) ucpy(d->ipa, sizeof(d->ipa), w->ipa);
        int ans = rnd(4), got = 1;
        d->answer = (int8_t)ans;
        ucpy(d->choice[ans], sizeof(d->choice[ans]), vi_en ? w->en : w->vi);
        for (int tries = 0; tries < 60 && got < 4; tries++) {
            const word_t *x = &s_w[rnd(s_nw)];
            const char *t = vi_en ? x->en : x->vi;
            bool dup = false;
            for (int k = 0; k < 4; k++) if (d->choice[k][0] && !strcasecmp(d->choice[k], t)) dup = true;
            if (dup || !t[0]) continue;
            int slot = 0;
            while (d->choice[slot][0]) slot++;
            ucpy(d->choice[slot], sizeof(d->choice[slot]), t);
            got++;
        }
        if (got < 4) { memset(d, 0, sizeof(*d)); continue; }   /* khong du phuong an khac nhau */
        if (w->seen < 0xFFFF) w->seen++;
        s->n++;
    }
    unlock();
    free(o);
    if (s->n == 0) return FD_STUDY_ERR_NO_DECK;
    for (int i = s->n - 1; i > 0; i--) {
        int j = rnd(i + 1);
        fd_study_q_t t = s->q[i]; s->q[i] = s->q[j]; s->q[j] = t;
    }
    snprintf(s->session_id, sizeof(s->session_id), "lq-%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    s->offline = true;
    return FD_STUDY_ERR_NONE;
}

/* Dem lan hien the. File: "id" = +1, hoac "id<TAB>count" sau khi nen. */
static void views_replay_locked(void)
{
    FILE *f = fopen(VLOG, "r");
    if (!f) return;
    char line[48];
    while (fgets(line, sizeof(line), f)) {
        char *tab = strchr(line, '\t');
        if (tab) *tab = 0;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!is_wid(line)) continue;
        word_t *w = find_word(line);
        if (!w) continue;
        unsigned add = tab ? (unsigned)strtoul(tab + 1, NULL, 10) : 1u;
        unsigned sum = (unsigned)w->views + (add ? add : 1u);
        w->views = sum > 65535u ? 65535u : (uint16_t)sum;
    }
    fclose(f);
}

static void views_compact(void)
{
    if (!s_mx) return;
    lock();
    FILE *f = fopen(VLOG_TMP, "w");
    bool ok = f != NULL;
    for (int i = 0; ok && i < s_nw; i++) {
        if (!s_w[i].views) continue;
        if (fprintf(f, "%s\t%u\n", s_w[i].id, (unsigned)s_w[i].views) < 0) ok = false;
    }
    if (f) {
        ok = fflush(f) == 0 && ok;
        ok = fsync(fileno(f)) == 0 && ok;
        ok = fclose(f) == 0 && ok;
    }
    if (ok) ok = rename(VLOG_TMP, VLOG) == 0;
    if (!ok) unlink(VLOG_TMP);
    unlock();
}

void fd_sync_card_seen(const char *wid)
{
    if (!s_mx || !is_wid(wid)) return;
    lock();
    word_t *w = find_word(wid);
    bool hit = w != NULL;
    if (hit && w->views < 65535) w->views++;
    unlock();
    if (!hit) return;
    mkdir(SDIR, 0775);
    FILE *f = fopen(VLOG, "a");
    if (!f) return;
    fprintf(f, "%s\n", wid);
    fflush(f);
    fsync(fileno(f));
    long sz = ftell(f);
    fclose(f);
    if (sz > 65536) views_compact();
}

/* It xem truoc. Cung muc do xem: chua thuoc truoc (neu bat), tu moi truoc (neu bat), roi ngau nhien. */
static int order_cards(ord_t *o, int cap)
{
    int n = 0;
    for (int i = 0; i < s_nw; i++) {
        if (!s_w[i].en[0] || !s_w[i].vi[0]) continue;
        uint64_t views = s_w[i].views > 255 ? 255 : s_w[i].views;
        uint64_t unl = (s_st.prio_unl && s_w[i].st == 2) ? 1 : 0;
        uint64_t sub = s_st.prio_new ? (uint64_t)(0xFFFFFFFFu - s_w[i].ca) : (uint64_t)esp_random();
        o[n].key = (views << 56) | (unl << 48) | ((sub & 0xFFFFFFFFull) << 16) | (esp_random() & 0xFFFF);
        o[n].idx = i;
        n++;
    }
    qsort(o, n, sizeof(*o), ord_cmp);
    return n < cap ? n : cap;
}

int fd_sync_build_cards(fd_fc_card_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    ord_t *o = ps_alloc(sizeof(ord_t) * FD_SYNC_MAX_WORDS);
    if (!o) return 0;
    lock();
    int m = s_st.have ? order_cards(o, cap) : 0;
    for (int i = 0; i < m; i++) {
        const word_t *w = &s_w[o[i].idx];
        memset(&out[i], 0, sizeof(out[i]));
        ucpy(out[i].id, sizeof(out[i].id), w->id);
        ucpy(out[i].word, sizeof(out[i].word), w->en);
        ucpy(out[i].ipa, sizeof(out[i].ipa), w->ipa);
        ucpy(out[i].meaning, sizeof(out[i].meaning), w->vi);
        ucpy(out[i].pos, sizeof(out[i].pos), w->pos);
    }
    unlock();
    free(o);
    return m;
}

/* Tach cau thanh the: 3..FD_SEN_TOK the, moi the < FD_SEN_TOK_LEN; dau cau cuoi .?! rieng. */
static bool make_ex(const char *en, const char *vi, fd_sen_ex_t *e)
{
    memset(e, 0, sizeof(*e));
    char buf[FD_SEN_TXT_LEN];
    if (!en || strlen(en) >= sizeof(buf)) return false;
    strlcpy(buf, en, sizeof(buf));
    size_t L = strlen(buf);
    while (L && buf[L - 1] == ' ') buf[--L] = 0;
    e->end[0] = '.';
    if (L && strchr(".?!", buf[L - 1])) { e->end[0] = buf[L - 1]; buf[--L] = 0; }
    char *save = NULL;
    for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
        if (e->n >= FD_SEN_TOK || strlen(t) >= FD_SEN_TOK_LEN || strpbrk(t, ".?!")) return false;
        strcpy(e->ans[e->n++], t);
    }
    if (e->n < 3) return false;
    memcpy(e->tok, e->ans, sizeof(e->tok));
    char s2[FD_SEN_TXT_LEN];
    s2[0] = 0;
    for (int i = 0; i < e->n; i++) { if (i) strlcat(s2, " ", sizeof(s2)); strlcat(s2, e->ans[i], sizeof(s2)); }
    strlcat(s2, e->end, sizeof(s2));
    ucpy(e->sentence, sizeof(e->sentence), s2);
    ucpy(e->hint, sizeof(e->hint), vi ? vi : "");
    return true;
}

int fd_sync_build_sentences(fd_sen_ex_t *out, char (*wid)[FD_SYNC_WID_LEN], int cap)
{
    if (!out || cap <= 0 || !fd_sync_ready()) return 0;
    ord_t *o = ps_alloc(sizeof(ord_t) * FD_SYNC_MAX_WORDS);
    char *line = ps_alloc(LINE_CAP);
    int got = 0;
    if (!o || !line) { free(o); free(line); return 0; }
    lock();
    int m = order_words(o, cap * 3, true);
    FILE *f = m ? fopen(STORE, "r") : NULL;
    for (int i = 0; f && i < m && got < cap; i++) {
        word_t *w = &s_w[o[i].idx];
        if (fseek(f, (long)w->off, SEEK_SET) || !fgets(line, LINE_CAP, f)) continue;
        size_t L = strlen(line);
        if (L && line[L - 1] == '\n') line[--L] = 0;
        char *fl[32];
        int n = split_tabs(line, fl, 32);
        int k = n >= 11 ? atoi(fl[10]) : 0;
        if (k <= 0 || 11 + 2 * k > n) continue;
        int pick = rnd(k);
        for (int t = 0; t < k; t++) {
            int j = (pick + t) % k;
            if (make_ex(fl[11 + 2 * j], fl[12 + 2 * j], &out[got])) {
                snprintf(out[got].id, sizeof(out[got].id), "l%s%d", w->id, j);
                if (wid) strlcpy(wid[got], w->id, FD_SYNC_WID_LEN);
                if (!out[got].hint[0]) ucpy(out[got].hint, sizeof(out[got].hint), w->vi);
                if (w->seen < 0xFFFF) w->seen++;
                got++;
                break;
            }
        }
    }
    if (f) fclose(f);
    unlock();
    free(line);
    free(o);
    return got;
}
/* ---------- 2.3.5: phan loai tu + dien cho trong ---------- */
typedef struct { const char *key; const char *vi; const char *en; } pos_map_t;
static const pos_map_t POS_MAP[] = {
    {"noun", "Danh từ", "Noun"}, {"n.", "Danh từ", "Noun"}, {"n", "Danh từ", "Noun"},
    {"verb", "Động từ", "Verb"}, {"v.", "Động từ", "Verb"}, {"v", "Động từ", "Verb"},
    {"adjective", "Tính từ", "Adjective"}, {"adj.", "Tính từ", "Adjective"}, {"adj", "Tính từ", "Adjective"},
    {"adverb", "Trạng từ", "Adverb"}, {"adv.", "Trạng từ", "Adverb"}, {"adv", "Trạng từ", "Adverb"},
    {"preposition", "Giới từ", "Preposition"}, {"prep.", "Giới từ", "Preposition"},
    {"pronoun", "Đại từ", "Pronoun"}, {"pron.", "Đại từ", "Pronoun"},
    {"conjunction", "Liên từ", "Conjunction"}, {"conj.", "Liên từ", "Conjunction"},
    {"interjection", "Thán từ", "Interjection"},
    {"determiner", "Hạn định từ", "Determiner"}, {"article", "Mạo từ", "Article"},
    {"phrase", "Cụm từ", "Phrase"},
};

static const char *pos_label(const char *raw, bool vi)
{
    static char buf[24];
    if (!raw || !raw[0]) return vi ? "Khác" : "Other";
    size_t n = 0;
    while (raw[n] && raw[n] != '/' && raw[n] != ',' && raw[n] != ';' && raw[n] != '|' && n + 1 < sizeof(buf)) {
        buf[n] = raw[n];
        n++;
    }
    while (n && buf[n - 1] == ' ') n--;
    buf[n] = 0;
    for (size_t i = 0; i < sizeof(POS_MAP) / sizeof(POS_MAP[0]); i++) {
        if (!strcasecmp(buf, POS_MAP[i].key)) return vi ? POS_MAP[i].vi : POS_MAP[i].en;
    }
    return buf;
}

static bool pos_same(const char *a, const char *b)
{
    return a && b && !strcasecmp(a, b);
}

fd_study_err_t fd_sync_build_pos_quiz(fd_study_session_t *s, int n)
{
    if (!s) return FD_STUDY_ERR_FORMAT;
    memset(s, 0, sizeof(*s));
    if (!fd_sync_ready()) return FD_STUDY_ERR_NO_DECK;
    if (n <= 0 || n > FD_STUDY_MAX) n = FD_STUDY_MAX;
    ord_t *o = ps_alloc(sizeof(ord_t) * FD_SYNC_MAX_WORDS);
    if (!o) return FD_STUDY_ERR_NOMEM;
    lock();
    int m = order_words(o, n * 2, false);
    static const char *FALLBACK_VI[] = {"Danh từ", "Động từ", "Tính từ", "Trạng từ"};
    bool use_vi = true;
    for (int q = 0; q < m && s->n < n; q++) {
        word_t *w = &s_w[o[q].idx];
        if (!w->pos[0]) continue;
        const char *ans = pos_label(w->pos, use_vi);
        fd_study_q_t *d = &s->q[s->n];
        memset(d, 0, sizeof(*d));
        ucpy(d->id, sizeof(d->id), w->id);
        snprintf(d->prompt, sizeof(d->prompt), "%s", w->en);
        if (w->ipa[0]) ucpy(d->ipa, sizeof(d->ipa), w->ipa);
        int slot = rnd(4), got = 1;
        d->answer = (int8_t)slot;
        ucpy(d->choice[slot], sizeof(d->choice[slot]), ans);
        for (int tries = 0; tries < 80 && got < 4; tries++) {
            const word_t *x = &s_w[rnd(s_nw)];
            const char *t = pos_label(x->pos, use_vi);
            if (!x->pos[0] || pos_same(t, ans)) continue;
            bool dup = false;
            for (int k = 0; k < 4; k++) if (d->choice[k][0] && pos_same(d->choice[k], t)) dup = true;
            if (dup) continue;
            int i = 0; while (d->choice[i][0]) i++;
            ucpy(d->choice[i], sizeof(d->choice[i]), t);
            got++;
        }
        for (int f = 0; got < 4 && f < 4; f++) {
            const char *t = FALLBACK_VI[f];
            if (pos_same(t, ans)) continue;
            bool dup = false;
            for (int k = 0; k < 4; k++) if (d->choice[k][0] && pos_same(d->choice[k], t)) dup = true;
            if (dup) continue;
            int i = 0; while (d->choice[i][0]) i++;
            ucpy(d->choice[i], sizeof(d->choice[i]), t);
            got++;
        }
        if (got < 4) continue;
        if (w->seen < 0xFFFF) w->seen++;
        s->n++;
    }
    unlock();
    free(o);
    if (s->n == 0) return FD_STUDY_ERR_NO_DECK;
    for (int i = s->n - 1; i > 0; i--) {
        int j = rnd(i + 1);
        fd_study_q_t t = s->q[i]; s->q[i] = s->q[j]; s->q[j] = t;
    }
    snprintf(s->session_id, sizeof(s->session_id), "lp-%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    s->offline = true;
    return FD_STUDY_ERR_NONE;
}

static bool cloze_blank(const char *sentence, const char *word, char *out, size_t cap)
{
    if (!sentence || !word || !word[0] || !out || cap < 8) return false;
    size_t wl = strlen(word);
    const char *p = sentence;
    const char *hit = NULL;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ') p++;
        size_t n = (size_t)(p - start);
        size_t cmp = n;
        while (cmp && strchr(".?!,;:\"'", start[cmp - 1])) cmp--;
        if (cmp == wl && !strncasecmp(start, word, wl)) { hit = start; break; }
    }
    if (!hit) return false;
    size_t pre = (size_t)(hit - sentence);
    if (pre >= cap) return false;
    memcpy(out, sentence, pre);
    size_t o = pre;
    const char *blank = "______";
    size_t bl = strlen(blank);
    if (o + bl + 1 >= cap) return false;
    memcpy(out + o, blank, bl); o += bl;
    const char *rest = hit;
    while (*rest && *rest != ' ') rest++;
    if (o + strlen(rest) + 1 >= cap) return false;
    memcpy(out + o, rest, strlen(rest) + 1);
    return true;
}

fd_study_err_t fd_sync_build_cloze_quiz(fd_study_session_t *s, int n)
{
    if (!s) return FD_STUDY_ERR_FORMAT;
    memset(s, 0, sizeof(*s));
    if (!fd_sync_ready()) return FD_STUDY_ERR_NO_DECK;
    if (n <= 0 || n > FD_STUDY_MAX) n = FD_STUDY_MAX;
    ord_t *o = ps_alloc(sizeof(ord_t) * FD_SYNC_MAX_WORDS);
    char *line = ps_alloc(LINE_CAP);
    if (!o || !line) { free(o); free(line); return FD_STUDY_ERR_NOMEM; }
    lock();
    int m = order_words(o, n * 3, true);
    FILE *f = m ? fopen(STORE, "r") : NULL;
    for (int i = 0; f && i < m && s->n < n; i++) {
        word_t *w = &s_w[o[i].idx];
        if (fseek(f, (long)w->off, SEEK_SET) || !fgets(line, LINE_CAP, f)) continue;
        size_t L = strlen(line);
        if (L && line[L - 1] == '\n') line[--L] = 0;
        char *fl[32];
        int nf = split_tabs(line, fl, 32);
        int k = nf >= 11 ? atoi(fl[10]) : 0;
        if (k <= 0 || 11 + 2 * k > nf) continue;
        int pick = rnd(k);
        bool ok = false;
        fd_study_q_t *d = &s->q[s->n];
        memset(d, 0, sizeof(*d));
        for (int t = 0; t < k; t++) {
            int j = (pick + t) % k;
            const char *sen = fl[11 + 2 * j];
            if (cloze_blank(sen, w->en, d->prompt, sizeof(d->prompt))) { ok = true; break; }
        }
        if (!ok) continue;
        ucpy(d->id, sizeof(d->id), w->id);
        if (w->ipa[0]) ucpy(d->ipa, sizeof(d->ipa), w->ipa);
        int ans = rnd(4), got = 1;
        d->answer = (int8_t)ans;
        ucpy(d->choice[ans], sizeof(d->choice[ans]), w->en);
        for (int tries = 0; tries < 60 && got < 4; tries++) {
            const word_t *x = &s_w[rnd(s_nw)];
            if (!x->en[0] || !strcasecmp(x->en, w->en)) continue;
            bool dup = false;
            for (int cidx = 0; cidx < 4; cidx++) if (d->choice[cidx][0] && !strcasecmp(d->choice[cidx], x->en)) dup = true;
            if (dup) continue;
            int slot = 0; while (d->choice[slot][0]) slot++;
            ucpy(d->choice[slot], sizeof(d->choice[slot]), x->en);
            got++;
        }
        if (got < 4) continue;
        if (w->seen < 0xFFFF) w->seen++;
        s->n++;
    }
    if (f) fclose(f);
    unlock();
    free(o);
    free(line);
    if (s->n == 0) return FD_STUDY_ERR_NO_DECK;
    for (int i = s->n - 1; i > 0; i--) {
        int j = rnd(i + 1);
        fd_study_q_t t = s->q[i]; s->q[i] = s->q[j]; s->q[j] = t;
    }
    snprintf(s->session_id, sizeof(s->session_id), "lc-%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    s->offline = true;
    return FD_STUDY_ERR_NONE;
}
