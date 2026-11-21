/* 4.8.0 Tro choi: danh sach + tai + cai goi tro choi (xem fd_game.h). KHONG log token / header.
 * Moi viec mang chay trong task rieng (8 KB, tu xoa khi xong), UI chi doc trang thai. Mot slot duy nhat /data/game/. */
#include "fd_game.h"
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"

static const char *TAG = "fd_game";

#define GDIR     "/data/game"
#define PKG      GDIR "/game.json"
#define PKG_OLD  GDIR "/game.old"
#define DL_TMP   "/data/game.dl"
#define LIST_TMP "/data/game_list.tmp"
#define LIST_MAX (16 * 1024)
#define HW       "fnk0104b"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_mx;            /* bao ve s_list (memcpy lon, khong dung critical) */
static StaticSemaphore_t s_mx_buf;
static fd_game_status_t s_st;
static fd_game_entry_t *s_list;           /* PSRAM, FD_GAME_LIST_MAX */
static bool s_busy;
static fd_game_entry_t s_job;             /* muc dang cai (ban sao) */

static void ensure_mx(void)
{
    if (s_mx) return;
    taskENTER_CRITICAL(&s_mux);
    if (!s_mx) s_mx = xSemaphoreCreateMutexStatic(&s_mx_buf);
    taskEXIT_CRITICAL(&s_mux);
}

static void *ps_calloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

/* chep UTF-8 khong cat giua ky tu */
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
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static int jint(const cJSON *o, const char *k, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* "x.y.z" -> so so sanh duoc; -1 neu sai dinh dang */
static long ver_num(const char *s)
{
    unsigned a, b, c;
    if (!s || sscanf(s, "%u.%u.%u", &a, &b, &c) != 3 || a > 999 || b > 999 || c > 999) return -1;
    return (long)a * 1000000L + (long)b * 1000L + (long)c;
}

/* phien ban firmware dang chay: "FDict One - 4.8.0[-dev]" -> 4.8.0 */
static long fw_num(void)
{
    const char *v = esp_app_get_description()->version;
    const char *p = strrchr(v, ' ');
    return ver_num(p ? p + 1 : v);
}

static void fw_semver(char *out, size_t n)
{
    const char *v = esp_app_get_description()->version;
    const char *p = strrchr(v, ' ');
    p = p ? p + 1 : v;
    size_t k = strspn(p, "0123456789.");
    if (k >= n) k = n - 1;
    memcpy(out, p, k);
    out[k] = 0;
}

static bool fw_ok(const char *min_fw)
{
    if (!min_fw || !min_fw[0]) return true;
    long need = ver_num(min_fw), have = fw_num();
    return need >= 0 && have >= 0 && have >= need;
}

static bool safe_id(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= FD_GAME_ID_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

/* chi duong dan tuong doi tren may chu FDict (token Bearer khong roi khoi may chu) */
static bool safe_path(const char *p)
{
    size_t n = strlen(p);
    if (n < 6 || n >= FD_GAME_PATH_LEN || strncmp(p, "/api/", 5) != 0 || strstr(p, "..")) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c <= ' ' || c >= 0x7F || c == '\\' || c == '#') return false;
    }
    return true;
}

static bool is_hex64(const char *s)
{
    if (strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static void set_state(fd_game_state_t st, const char *err, bool install)
{
    taskENTER_CRITICAL(&s_mux);
    s_st.state = st;
    s_st.install = install;
    if (err) strlcpy(s_st.err, err, sizeof(s_st.err));
    else s_st.err[0] = 0;
    if (st != FD_GAME_DOWNLOADING) s_busy = false;
    taskEXIT_CRITICAL(&s_mux);
}

static void http_err(char *out, size_t n, int code)
{
    if (code == 404) strlcpy(out, "none", n);
    else if (code == 401) strlcpy(out, "unpaired", n);
    else if (code == -2) strlcpy(out, "too_large", n);
    else if (code == -3) strlcpy(out, "fs", n);
    else if (code < 0) strlcpy(out, "net", n);
    else snprintf(out, n, "http_%d", code);
}

/* doc ca file vao PSRAM (+ NUL). NULL neu loi / qua lon. */
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
    if (ok && ferror(f)) ok = false;
    uint8_t h[32];
    size_t hl = 0;
    if (ok && (psa_hash_finish(&op, h, sizeof(h), &hl) != PSA_SUCCESS || hl != 32)) ok = false;
    if (!ok) psa_hash_abort(&op);
    free(buf);
    fclose(f);
    if (ok) for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
    return ok;
}

/* ---------- goi ---------- */
void fd_game_cfg_default(fd_game_cfg_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->fall_ms = 4500;
    c->min_ms = 1300;
    c->every = 5;
    c->ramp_pct = 90;
    c->lives = 3;
}

/* Kiem JSON goi, dien cfg. Tra false neu khong phai goi Hung tu hop le. */
static bool parse_pkg(const cJSON *j, fd_game_cfg_t *c)
{
    fd_game_cfg_default(c);
    if (!cJSON_IsObject(j) || jint(j, "format", 0) != 1) return false;
    if (strcmp(jstr(j, "type"), "catch") != 0) return false;
    const char *id = jstr(j, "id");
    if (!safe_id(id) || !fw_ok(jstr(j, "min_fw"))) return false;
    ucpy(c->id, sizeof(c->id), id);
    ucpy(c->name, sizeof(c->name), jstr(j, "name")[0] ? jstr(j, "name") : id);
    ucpy(c->version, sizeof(c->version), jstr(j, "version"));
    const cJSON *cf = cJSON_GetObjectItemCaseSensitive(j, "config");
    if (cJSON_IsObject(cf)) {
        c->fall_ms = clampi(jint(cf, "fall_ms", c->fall_ms), 800, 15000);
        c->min_ms = clampi(jint(cf, "min_ms", c->min_ms), 500, c->fall_ms);
        c->every = clampi(jint(cf, "every", c->every), 1, 50);
        c->ramp_pct = clampi(jint(cf, "ramp_pct", c->ramp_pct), 50, 99);
        c->lives = clampi(jint(cf, "lives", c->lives), 1, 9);
    }
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(j, "words");
    int n = 0;
    if (cJSON_IsArray(w)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, w) {
            if (n >= FD_GAME_WORDS_MAX) break;
            if (jstr(it, "en")[0] && jstr(it, "vi")[0]) n++;
        }
    }
    c->nwords = n;
    return true;
}

static cJSON *load_pkg_json(void)
{
    size_t len = 0;
    char *buf = read_all(PKG, FD_GAME_PKG_MAX, &len);
    if (!buf) return NULL;
    cJSON *j = cJSON_ParseWithLength(buf, len);
    free(buf);
    return j;
}

bool fd_game_installed(fd_game_cfg_t *out)
{
    fd_game_cfg_t c;
    cJSON *j = load_pkg_json();
    bool ok = j && parse_pkg(j, &c);
    cJSON_Delete(j);
    if (!ok) fd_game_cfg_default(&c);
    if (out) *out = c;
    return ok;
}

int fd_game_pkg_words(fd_spell_word_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    cJSON *j = load_pkg_json();
    fd_game_cfg_t c;
    int n = 0;
    if (j && parse_pkg(j, &c)) {
        const cJSON *w = cJSON_GetObjectItemCaseSensitive(j, "words");
        const cJSON *it;
        cJSON_ArrayForEach(it, w) {
            if (n >= cap) break;
            const char *en = jstr(it, "en"), *vi = jstr(it, "vi");
            if (!en[0] || !vi[0] || strlen(en) >= sizeof(out->en) || strlen(vi) >= sizeof(out->vi)) continue;
            fd_spell_word_t *d = &out[n++];
            memset(d, 0, sizeof(*d));
            ucpy(d->en, sizeof(d->en), en);
            ucpy(d->vi, sizeof(d->vi), vi);
            ucpy(d->ipa, sizeof(d->ipa), jstr(it, "ipa"));
        }
    }
    cJSON_Delete(j);
    for (int i = n - 1; i > 0; i--) {
        int k = (int)(esp_random() % (uint32_t)(i + 1));
        fd_spell_word_t t = out[i]; out[i] = out[k]; out[k] = t;
    }
    return n;
}

/* ---------- danh sach ---------- */
static void list_task(void *arg)
{
    (void)arg;
    char fw[16], path[96], err[24] = "";
    fw_semver(fw, sizeof(fw));
    snprintf(path, sizeof(path), "/api/devices/games?hw=" HW "&fw=%s", fw);
    int code = fd_ota_api_get_file_q(path, LIST_TMP, LIST_MAX, NULL, NULL);
    ESP_LOGI(TAG, "list: HTTP %d", code);
    fd_game_cfg_t inst;
    bool have = fd_game_installed(&inst);
    int n = 0;
    fd_game_entry_t *tmp = ps_calloc(sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
    if (code != 200) {
        http_err(err, sizeof(err), code);
    } else if (!tmp) {
        strlcpy(err, "nomem", sizeof(err));
    } else {
        size_t len = 0;
        char *buf = read_all(LIST_TMP, LIST_MAX, &len);
        cJSON *j = buf ? cJSON_ParseWithLength(buf, len) : NULL;
        free(buf);
        const cJSON *g = j ? cJSON_GetObjectItemCaseSensitive(j, "games") : NULL;
        if (!cJSON_IsArray(g)) {
            strlcpy(err, "format", sizeof(err));
        } else {
            const cJSON *it;
            cJSON_ArrayForEach(it, g) {
                if (n >= FD_GAME_LIST_MAX) break;
                const char *id = jstr(it, "id"), *pth = jstr(it, "path"), *sha = jstr(it, "sha256");
                if (!safe_id(id) || !safe_path(pth) || !is_hex64(sha)) continue;
                fd_game_entry_t *e = &tmp[n++];
                ucpy(e->id, sizeof(e->id), id);
                ucpy(e->name, sizeof(e->name), jstr(it, "name")[0] ? jstr(it, "name") : id);
                ucpy(e->version, sizeof(e->version), jstr(it, "version"));
                ucpy(e->min_fw, sizeof(e->min_fw), jstr(it, "min_fw"));
                ucpy(e->path, sizeof(e->path), pth);
                for (int k = 0; k < 64; k++) e->sha256[k] = (char)((sha[k] >= 'A' && sha[k] <= 'F') ? sha[k] + 32 : sha[k]);
                e->sha256[64] = 0;
                e->size = clampi(jint(it, "size", 0), 0, FD_GAME_PKG_MAX + 1);
                e->supported = !strcmp(jstr(it, "type"), "catch") && fw_ok(e->min_fw) && e->size <= FD_GAME_PKG_MAX;
                e->installed = have && !strcmp(inst.id, e->id) && !strcmp(inst.version, e->version);
            }
        }
        cJSON_Delete(j);
    }
    unlink(LIST_TMP);
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (!s_list) s_list = ps_calloc(sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
    if (s_list && tmp) memcpy(s_list, tmp, sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
    else if (!err[0]) strlcpy(err, "nomem", sizeof(err));
    xSemaphoreGive(s_mx);
    free(tmp);
    taskENTER_CRITICAL(&s_mux);
    s_st.n = err[0] ? 0 : n;
    taskEXIT_CRITICAL(&s_mux);
    set_state(err[0] ? FD_GAME_ERROR : FD_GAME_LIST_READY, err[0] ? err : NULL, false);
    vTaskDelete(NULL);
}

static bool begin(fd_game_state_t st, bool install)
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

void fd_game_list_fetch(void)
{
    if (!begin(FD_GAME_LISTING, false)) return;
    if (!fd_net_is_connected()) { set_state(FD_GAME_ERROR, "no_wifi", false); return; }
    if (!fd_ota_is_paired()) { set_state(FD_GAME_ERROR, "unpaired", false); return; }
    if (xTaskCreate(list_task, "fd_game", 8192, NULL, 3, NULL) != pdPASS) set_state(FD_GAME_ERROR, "nomem", false);
}

void fd_game_get(fd_game_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mux);
    *out = s_st;
    taskEXIT_CRITICAL(&s_mux);
}

int fd_game_list(fd_game_entry_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    fd_game_status_t st;
    fd_game_get(&st);
    int n = st.n < cap ? st.n : cap;
    if (n <= 0) return 0;
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (s_list) memcpy(out, s_list, sizeof(fd_game_entry_t) * (size_t)n);
    else n = 0;
    xSemaphoreGive(s_mx);
    return n;
}

/* ---------- cai ---------- */
static void prog_cb(int got, int total, void *ctx)
{
    (void)ctx;
    int pct = total > 0 ? (int)((int64_t)got * 100 / total) : 0;
    taskENTER_CRITICAL(&s_mux);
    s_st.pct = clampi(pct, 0, 100);
    taskEXIT_CRITICAL(&s_mux);
}

static void install_task(void *arg)
{
    (void)arg;
    const fd_game_entry_t *e = &s_job;
    char err[24] = "", sha[65] = "";
    unlink(DL_TMP);
    int code = fd_ota_api_get_file_q(e->path, DL_TMP, FD_GAME_PKG_MAX, prog_cb, NULL);
    ESP_LOGI(TAG, "install %s %s: HTTP %d", e->id, e->version, code);
    struct stat sb;
    if (code != 200) {
        http_err(err, sizeof(err), code);
    } else if (stat(DL_TMP, &sb) != 0 || sb.st_size <= 0 || (e->size > 0 && sb.st_size != e->size)) {
        strlcpy(err, "format", sizeof(err));
    } else if (!sha256_file(DL_TMP, sha) || strcmp(sha, e->sha256) != 0) {
        strlcpy(err, "sha256", sizeof(err));
    } else {
        size_t len = 0;
        char *buf = read_all(DL_TMP, FD_GAME_PKG_MAX, &len);
        bool got = buf != NULL;
        cJSON *j = got ? cJSON_ParseWithLength(buf, len) : NULL;
        free(buf);
        fd_game_cfg_t c;
        if (!j) strlcpy(err, got ? "format" : "nomem", sizeof(err));
        else if (!fw_ok(jstr(j, "min_fw"))) strlcpy(err, "min_fw", sizeof(err));
        else if (!parse_pkg(j, &c) || strcmp(c.id, e->id) != 0) strlcpy(err, "format", sizeof(err));
        cJSON_Delete(j);
    }
    if (!err[0]) {
        /* thay goi cu: game.json -> game.old, game.dl -> game.json, xoa game.old (loi giua chung: tra lai goi cu) */
        if (mkdir(GDIR, 0775) != 0 && errno != EEXIST) strlcpy(err, "fs", sizeof(err));
        unlink(PKG_OLD);
        bool had = !err[0] && stat(PKG, &sb) == 0;
        if (!err[0] && had && rename(PKG, PKG_OLD) != 0) strlcpy(err, "fs", sizeof(err));
        if (!err[0] && rename(DL_TMP, PKG) != 0) {
            strlcpy(err, "fs", sizeof(err));
            if (had) rename(PKG_OLD, PKG);
        }
        unlink(PKG_OLD);
    }
    unlink(DL_TMP);
    ESP_LOGI(TAG, "install %s: %s", e->id, err[0] ? err : "ok");
    set_state(err[0] ? FD_GAME_ERROR : FD_GAME_DONE, err[0] ? err : NULL, true);
    vTaskDelete(NULL);
}

bool fd_game_install(const fd_game_entry_t *e)
{
    if (!e || !e->supported || !safe_id(e->id) || !safe_path(e->path) || !is_hex64(e->sha256)) return false;
    if (!begin(FD_GAME_DOWNLOADING, true)) return false;
    s_job = *e;
    if (!fd_net_is_connected()) { set_state(FD_GAME_ERROR, "no_wifi", true); return true; }
    if (xTaskCreate(install_task, "fd_game", 8192, NULL, 3, NULL) != pdPASS) set_state(FD_GAME_ERROR, "nomem", true);
    return true;
}
