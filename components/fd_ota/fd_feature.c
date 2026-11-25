/* Tai tinh nang: danh sach + goi co ma nguon. Khong log token. */
#include "fd_feature.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"

static const char *TAG = "fd_feat";

#define ROOT "/data/features"
#define LIST_CACHE ROOT "/list.json"
#define DL_TMP "/data/feature.dl"
#define LIST_TMP "/data/feature_list.tmp"
#define LIST_MAX (16 * 1024)

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_mx;
static StaticSemaphore_t s_mx_buf;
static fd_feature_status_t s_st;
static EXT_RAM_BSS_ATTR fd_feature_entry_t s_list[FD_FEAT_LIST_MAX];
static int s_n;
static bool s_busy;
static bool s_cache_loaded;
static bool s_have_list;
static bool s_bad_loaded;
static char s_bad_ver[FD_FEAT_VER_LEN];
static fd_feature_entry_t s_job;

static void ensure_mx(void)
{
    if (s_mx) return;
    taskENTER_CRITICAL(&s_mux);
    if (!s_mx) s_mx = xSemaphoreCreateMutexStatic(&s_mx_buf);
    taskEXIT_CRITICAL(&s_mux);
}

static void ucpy(char *out, size_t cap, const char *in)
{
    size_t n = in ? strlen(in) : 0;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(out, in, n);
    out[n] = 0;
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static int jint(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
}

static long ver_num(const char *s)
{
    unsigned a, b, c;
    if (!s || sscanf(s, "%u.%u.%u", &a, &b, &c) != 3 || a > 999 || b > 999 || c > 999) return -1;
    return (long)a * 1000000L + (long)b * 1000L + (long)c;
}

static bool fw_ok(const char *min_fw)
{
    if (!min_fw || !min_fw[0]) return true;
    const char *v = esp_app_get_description()->version;
    const char *p = strrchr(v, ' ');
    long have = ver_num(p ? p + 1 : v);
    long need = ver_num(min_fw);
    return need >= 0 && have >= need;
}

static bool safe_id(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n >= FD_FEAT_ID_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

static bool safe_path(const char *p)
{
    size_t n = p ? strlen(p) : 0;
    if (n < 6 || n >= FD_FEAT_PATH_LEN || strncmp(p, "/api/", 5) != 0 || strstr(p, "..")) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c <= ' ' || c >= 0x7F) return false;
    }
    return true;
}

static bool is_hex64(const char *s)
{
    if (!s || strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static void set_state(fd_feature_state_t st, const char *err, bool install)
{
    taskENTER_CRITICAL(&s_mux);
    s_st.state = st;
    s_st.install = install;
    if (err) strlcpy(s_st.err, err, sizeof(s_st.err));
    else s_st.err[0] = 0;
    if (st != FD_FEAT_DOWNLOADING) s_busy = false;
    taskEXIT_CRITICAL(&s_mux);
}

static void http_err(char *out, size_t n, int code)
{
    if (code == 404) strlcpy(out, "none", n);
    else if (code == 401) strlcpy(out, "unpaired", n);
    else if (code < 0) strlcpy(out, "net", n);
    else snprintf(out, n, "http_%d", code);
}

static char *read_all(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz > 0 && (size_t)sz <= max && fseek(f, 0, SEEK_SET) == 0) {
            buf = heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) buf = malloc((size_t)sz + 1);
            if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
                buf[sz] = 0;
                if (len) *len = (size_t)sz;
            } else {
                free(buf);
                buf = NULL;
            }
        }
    }
    fclose(f);
    return buf;
}

static bool write_atomic(const char *tmp, const char *dst, const char *buf, size_t len)
{
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = len == 0 || fwrite(buf, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp, dst) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

static bool sha256_file(const char *path, char out[65])
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    psa_crypto_init();
    uint8_t *buf = malloc(2048);
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool ok = buf && psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS;
    size_t r;
    while (ok && (r = fread(buf, 1, 2048, f)) > 0) {
        if (psa_hash_update(&op, buf, r) != PSA_SUCCESS) ok = false;
    }
    uint8_t h[32];
    size_t hl = 0;
    if (ok && (psa_hash_finish(&op, h, sizeof(h), &hl) != PSA_SUCCESS || hl != 32)) ok = false;
    if (!ok) psa_hash_abort(&op);
    free(buf);
    fclose(f);
    if (ok) for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
    return ok;
}

static void slot_paths(const char *id, char *dir, size_t dn, char *meta, size_t mn)
{
    snprintf(dir, dn, "/data/features/%.31s", id);
    snprintf(meta, mn, "/data/features/%.31s/meta.json", id);
}

bool fd_feature_local(const char *id, char *version, size_t n)
{
    if (version && n) version[0] = 0;
    if (!safe_id(id)) return false;
    char meta[80];
    snprintf(meta, sizeof(meta), "/data/features/%.31s/meta.json", id);
    size_t len = 0;
    char *buf = read_all(meta, 512, &len);
    if (!buf) return false;
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    const char *ver = j ? jstr(j, "version") : "";
    bool ok = ver[0] != 0;
    if (ok && version && n) ucpy(version, n, ver);
    cJSON_Delete(j);
    return ok;
}

static bool entry_eq(const fd_feature_entry_t *a, const fd_feature_entry_t *b)
{
    return !strcmp(a->id, b->id) && !strcmp(a->version, b->version) && !strcmp(a->min_fw, b->min_fw) &&
           !strcmp(a->sha256, b->sha256) && !strcmp(a->path, b->path) && a->size == b->size &&
           a->supported == b->supported;
}

static int parse_list(const char *buf, size_t len, fd_feature_entry_t *tmp, int cap)
{
    cJSON *j = buf ? cJSON_ParseWithLength(buf, len) : NULL;
    const cJSON *arr = j ? cJSON_GetObjectItemCaseSensitive(j, "features") : NULL;
    int n = -1;
    if (cJSON_IsArray(arr)) {
        n = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            const char *id = jstr(it, "id");
            const char *path = jstr(it, "path");
            const char *sha = jstr(it, "sha256");
            if (!safe_id(id) || !safe_path(path) || !is_hex64(sha)) continue;
            fd_feature_entry_t cand;
            memset(&cand, 0, sizeof(cand));
            ucpy(cand.id, sizeof(cand.id), id);
            ucpy(cand.version, sizeof(cand.version), jstr(it, "version"));
            ucpy(cand.min_fw, sizeof(cand.min_fw), jstr(it, "min_fw"));
            ucpy(cand.path, sizeof(cand.path), path);
            for (int k = 0; k < 64; k++) cand.sha256[k] = (char)((sha[k] >= 'A' && sha[k] <= 'F') ? sha[k] + 32 : sha[k]);
            cand.size = jint(it, "size");
            if (cand.size < 0 || cand.size > FD_FEAT_PKG_MAX) cand.size = 0;
            cand.supported = !strcmp(jstr(it, "type"), "shadow") && fw_ok(cand.min_fw);
            int dup = -1;
            for (int k = 0; k < n; k++) if (!strcmp(tmp[k].id, cand.id)) { dup = k; break; }
            if (dup >= 0) {
                bool take = (cand.supported && !tmp[dup].supported) ||
                            (cand.supported == tmp[dup].supported && ver_num(cand.version) > ver_num(tmp[dup].version));
                if (take) tmp[dup] = cand;
            } else if (n < cap) {
                tmp[n++] = cand;
            }
        }
    }
    cJSON_Delete(j);
    return n;
}

static bool begin(fd_feature_state_t st, bool install)
{
    taskENTER_CRITICAL(&s_mux);
    if (s_busy) { taskEXIT_CRITICAL(&s_mux); return false; }
    s_busy = true;
    s_st.state = st;
    s_st.install = install;
    s_st.pct = 0;
    s_st.err[0] = 0;
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

static void load_shadow_bad_locked(void)
{
    if (s_bad_loaded) return;
    s_bad_loaded = true;
    s_bad_ver[0] = 0;
    size_t len = 0;
    char *b = read_all("/data/features/shadow/bad", FD_FEAT_VER_LEN - 1, &len);
    if (b) {
        ucpy(s_bad_ver, sizeof(s_bad_ver), b);
        free(b);
    }
}

void fd_feature_load_cache(void)
{
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (!s_cache_loaded) {
        s_cache_loaded = true;
        unlink(LIST_TMP);
        size_t len = 0;
        char *buf = read_all(LIST_CACHE, LIST_MAX, &len);
        if (buf) {
            fd_feature_entry_t tmp[FD_FEAT_LIST_MAX];
            memset(tmp, 0, sizeof(tmp));
            int n = parse_list(buf, len, tmp, FD_FEAT_LIST_MAX);
            free(buf);
            if (n >= 0) {
                s_n = n;
                if (n > 0) memcpy(s_list, tmp, sizeof(tmp[0]) * (size_t)n);
                s_have_list = true;
            }
        }
        load_shadow_bad_locked();
    }
    xSemaphoreGive(s_mx);
}

bool fd_feature_has_list(void)
{
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool have = s_have_list;
    xSemaphoreGive(s_mx);
    return have;
}

bool fd_feature_apply_sync(const char *buf, size_t len)
{
    if (!buf || len == 0 || len > LIST_MAX) return false;
    fd_feature_entry_t tmp[FD_FEAT_LIST_MAX];
    memset(tmp, 0, sizeof(tmp));
    int n = parse_list(buf, len, tmp, FD_FEAT_LIST_MAX);
    if (n < 0) return false;
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    unlink(LIST_TMP);
    bool same = s_have_list && n == s_n;
    for (int i = 0; same && i < n; i++) same = entry_eq(&s_list[i], &tmp[i]);
    bool ok = true;
    if (!same) {
        mkdir(ROOT, 0775);
        ok = write_atomic(LIST_TMP, LIST_CACHE, buf, len);
        if (ok) {
            s_n = n;
            if (n > 0) memcpy(s_list, tmp, sizeof(tmp[0]) * (size_t)n);
            s_have_list = true;
            s_cache_loaded = true;
        }
    }
    xSemaphoreGive(s_mx);
    if (ok && !same) {
        taskENTER_CRITICAL(&s_mux);
        s_st.list_seq++;
        taskEXIT_CRITICAL(&s_mux);
    }
    return ok;
}

void fd_feature_mark_broken(const char *id)
{
    if (!safe_id(id)) return;
    char ver[FD_FEAT_VER_LEN] = "";
    if (!fd_feature_local(id, ver, sizeof(ver)) || !ver[0]) strlcpy(ver, "*", sizeof(ver));
    char dir[64], bad[80], tmp[88];
    snprintf(dir, sizeof(dir), ROOT "/%.31s", id);
    snprintf(bad, sizeof(bad), ROOT "/%.31s/bad", id);
    snprintf(tmp, sizeof(tmp), ROOT "/%.31s/bad.tmp", id);
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    mkdir(ROOT, 0775);
    mkdir(dir, 0775);
    if (write_atomic(tmp, bad, ver, strlen(ver)) && !strcmp(id, "shadow")) {
        s_bad_loaded = true;
        ucpy(s_bad_ver, sizeof(s_bad_ver), ver);
    }
    xSemaphoreGive(s_mx);
}

bool fd_feature_is_broken(const char *id, const char *installed_ver)
{
    if (!safe_id(id)) return false;
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (!strcmp(id, "shadow")) {
        load_shadow_bad_locked();
        bool b = s_bad_ver[0] != 0 && (!strcmp(s_bad_ver, "*") || !installed_ver || !installed_ver[0] || !strcmp(s_bad_ver, installed_ver));
        xSemaphoreGive(s_mx);
        return b;
    }
    char bad[80];
    snprintf(bad, sizeof(bad), ROOT "/%.31s/bad", id);
    size_t len = 0;
    char *buf = read_all(bad, FD_FEAT_VER_LEN - 1, &len);
    bool b = false;
    if (buf) {
        b = buf[0] != 0 && (!strcmp(buf, "*") || !installed_ver || !installed_ver[0] || !strcmp(buf, installed_ver));
        free(buf);
    }
    xSemaphoreGive(s_mx);
    return b;
}

void fd_feature_get(fd_feature_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mux);
    *out = s_st;
    taskEXIT_CRITICAL(&s_mux);
}

bool fd_feature_find(const char *id, fd_feature_entry_t *out)
{
    if (!id || !out) return false;
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool ok = false;
    for (int i = 0; i < s_n; i++) if (!strcmp(s_list[i].id, id)) { *out = s_list[i]; ok = true; break; }
    xSemaphoreGive(s_mx);
    return ok;
}

static void prog_cb(int got, int total, void *ctx)
{
    (void)ctx;
    int pct = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
    taskENTER_CRITICAL(&s_mux);
    s_st.pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    taskEXIT_CRITICAL(&s_mux);
}

static bool write_text(const char *path, const char *txt)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    size_t n = strlen(txt);
    bool ok = fwrite(txt, 1, n, f) == n;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) unlink(path);
    return ok;
}

static void install_task(void *arg)
{
    (void)arg;
    const fd_feature_entry_t *e = &s_job;
    char err[24] = "", sha[65] = "";
    unlink(DL_TMP);
    int code = fd_ota_api_get_file_q(e->path, DL_TMP, FD_FEAT_PKG_MAX, prog_cb, NULL);
    ESP_LOGI(TAG, "install %s %s: HTTP %d", e->id, e->version, code);
    struct stat sb;
    cJSON *j = NULL;
    if (code != 200) http_err(err, sizeof(err), code);
    else if (stat(DL_TMP, &sb) != 0 || sb.st_size <= 0 || (e->size > 0 && sb.st_size != e->size)) strlcpy(err, "format", sizeof(err));
    else if (!sha256_file(DL_TMP, sha) || strcmp(sha, e->sha256) != 0) strlcpy(err, "sha256", sizeof(err));
    else {
        size_t len = 0;
        char *buf = read_all(DL_TMP, FD_FEAT_PKG_MAX, &len);
        j = buf ? cJSON_ParseWithLength(buf, len) : NULL;
        free(buf);
        const char *script = j ? jstr(j, "script") : "";
        if (!j || strcmp(jstr(j, "id"), e->id) != 0 || strcmp(jstr(j, "type"), "shadow") != 0 || !strstr(script, e->id))
            strlcpy(err, "format", sizeof(err));
        else if (!fw_ok(jstr(j, "min_fw"))) strlcpy(err, "min_fw", sizeof(err));
    }
    if (!err[0]) {
        char dir[64], meta[80], pkg[80];
        slot_paths(e->id, dir, sizeof(dir), meta, sizeof(meta));
        snprintf(pkg, sizeof(pkg), "%.46s/feature.json", dir);
        if ((mkdir(ROOT, 0775) != 0 && errno != EEXIST) || (mkdir(dir, 0775) != 0 && errno != EEXIST)) strlcpy(err, "fs", sizeof(err));
        else if (rename(DL_TMP, pkg) != 0) strlcpy(err, "fs", sizeof(err));
        else {
            char body[96], bad[80];
            snprintf(body, sizeof(body), "{\"id\":\"%.31s\",\"version\":\"%.15s\",\"type\":\"shadow\"}", e->id, e->version);
            if (!write_text(meta, body)) strlcpy(err, "fs", sizeof(err));
            else {
                snprintf(bad, sizeof(bad), ROOT "/%.31s/bad", e->id);
                unlink(bad);
                ensure_mx();
                xSemaphoreTake(s_mx, portMAX_DELAY);
                if (!strcmp(e->id, "shadow")) { s_bad_loaded = true; s_bad_ver[0] = 0; }
                xSemaphoreGive(s_mx);
            }
        }
    }
    cJSON_Delete(j);
    unlink(DL_TMP);
    ESP_LOGI(TAG, "install %s: %s", e->id, err[0] ? err : "ok");
    set_state(err[0] ? FD_FEAT_ERROR : FD_FEAT_DONE, err[0] ? err : NULL, true);
    vTaskDelete(NULL);
}

bool fd_feature_install(const fd_feature_entry_t *e)
{
    if (!e || !e->supported || !safe_id(e->id) || !safe_path(e->path) || !is_hex64(e->sha256)) return false;
    if (!begin(FD_FEAT_DOWNLOADING, true)) return false;
    s_job = *e;
    if (!fd_net_is_connected()) { set_state(FD_FEAT_ERROR, "no_wifi", true); return true; }
    if (xTaskCreate(install_task, "fd_feat", 8192, NULL, 3, NULL) != pdPASS) set_state(FD_FEAT_ERROR, "nomem", true);
    return true;
}

/* 5.4.0: ma nguon Berry cua goi da cai (PSRAM, nguoi goi free). NULL neu chua cai, hong hoac rong. */
char *fd_feature_script(const char *id, size_t *len)
{
    if (len) *len = 0;
    if (!safe_id(id)) return NULL;
    char path[80];
    snprintf(path, sizeof(path), "/data/features/%.31s/feature.json", id);
    size_t n = 0;
    char *buf = read_all(path, FD_FEAT_PKG_MAX, &n);
    if (!buf) return NULL;
    cJSON *j = cJSON_ParseWithLength(buf, n);
    free(buf);
    const char *sc = j ? jstr(j, "script") : "";
    size_t sl = strlen(sc);
    char *out = NULL;
    if (sl > 0 && sl <= FD_FEAT_SCRIPT_MAX) {
        out = heap_caps_malloc(sl + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (out) {
            memcpy(out, sc, sl + 1);
            if (len) *len = sl;
        }
    }
    cJSON_Delete(j);
    return out;
}
