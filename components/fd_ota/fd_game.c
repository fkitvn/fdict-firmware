/* 4.8.0 Tro choi: danh sach + tai + cai goi tro choi (xem fd_game.h). KHONG log token / header.
 * Moi viec mang chay trong task rieng (8 KB, tu xoa khi xong), UI chi doc trang thai.
 * 4.9.7: nhieu slot /data/games/<id>/ (game.json = goi tai ve, meta.json = goi bo script), cache danh sach
 * /data/games/list.json; slot cu /data/game/ duoc chuyen sang khi dung lan dau. */
#include "fd_game.h"
#include <dirent.h>
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

#define GROOT      "/data/games"
#define LIST_CACHE GROOT "/list.json"
#define LEGACY_DIR "/data/game"            /* 4.8.0 - 4.9.6: mot slot */
#define DL_TMP     "/data/game.dl"
#define LIST_TMP   "/data/game_list.tmp"
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

/* 4.9.2: ten icon: chi [a-z0-9_-], qua dai hoac ky tu la -> rong (firmware chon icon theo type) */
static void icon_cpy(char out[FD_GAME_ICON_LEN], const char *v)
{
    out[0] = 0;
    size_t n = strlen(v);
    if (n == 0 || n >= FD_GAME_ICON_LEN) return;
    for (size_t i = 0; i < n; i++) {
        char ch = v[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return;
    }
    memcpy(out, v, n + 1);
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

/* Kiem JSON goi (hoac meta = goi bo script), dien cfg. Chi type berry (4.9.6 bo tro choi dung san / type catch). */
static bool parse_pkg(const cJSON *j, fd_game_cfg_t *c, bool need_script)
{
    fd_game_cfg_default(c);
    if (!cJSON_IsObject(j) || jint(j, "format", 0) != 1) return false;
    if (strcmp(jstr(j, "type"), "berry") != 0) return false;
    if (need_script) {
        size_t sl = strlen(jstr(j, "script"));
        if (sl == 0 || sl > FD_GAME_SCRIPT_MAX) return false;
    }
    const char *id = jstr(j, "id");
    if (!safe_id(id)) return false;
    ucpy(c->id, sizeof(c->id), id);
    ucpy(c->name, sizeof(c->name), jstr(j, "name")[0] ? jstr(j, "name") : id);
    ucpy(c->version, sizeof(c->version), jstr(j, "version"));
    ucpy(c->min_fw, sizeof(c->min_fw), jstr(j, "min_fw"));
    c->runnable = fw_ok(c->min_fw);
    icon_cpy(c->icon, jstr(j, "icon"));
    const cJSON *cf = cJSON_GetObjectItemCaseSensitive(j, "config");
    if (cJSON_IsObject(cf)) {
        c->fall_ms = clampi(jint(cf, "fall_ms", c->fall_ms), 800, 15000);
        c->min_ms = clampi(jint(cf, "min_ms", c->min_ms), 500, c->fall_ms);
        c->every = clampi(jint(cf, "every", c->every), 1, 50);
        c->ramp_pct = clampi(jint(cf, "ramp_pct", c->ramp_pct), 50, 99);
        c->lives = clampi(jint(cf, "lives", c->lives), 1, 9);
    }
    return true;
}

static void slot_file(char *out, size_t n, const char *id, const char *file)
{
    snprintf(out, n, GROOT "/%s/%s", id, file);
}

static cJSON *load_json(const char *path, size_t max)
{
    size_t len = 0;
    char *buf = read_all(path, max, &len);
    if (!buf) return NULL;
    cJSON *j = cJSON_ParseWithLength(buf, len);
    free(buf);
    return j;
}

/* ghi file qua .tmp + rename (khong de file do dang khi mat dien) */
static bool write_file(const char *path, const char *txt)
{
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    size_t n = strlen(txt);
    bool ok = fwrite(txt, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (ok) {
        unlink(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) unlink(tmp);
    return ok;
}

/* meta.json = goi bo "script" (doc nhanh khi liet ke) */
static bool write_meta(cJSON *pkg, const char *id)
{
    char path[80];
    slot_file(path, sizeof(path), id, "meta.json");
    cJSON *m = cJSON_Duplicate(pkg, true);
    if (!m) return false;
    cJSON_DeleteItemFromObjectCaseSensitive(m, "script");
    char *txt = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    bool ok = txt && write_file(path, txt);
    cJSON_free(txt);
    return ok;
}

/* Dua file goi da kiem (src) vao slot cua id: game.json cu -> game.old, src -> game.json, ghi meta. */
static bool slot_put(const char *src, cJSON *pkg, const char *id)
{
    char dir[64], cur[80], old[80];
    snprintf(dir, sizeof(dir), GROOT "/%s", id);
    if ((mkdir(GROOT, 0775) != 0 && errno != EEXIST) || (mkdir(dir, 0775) != 0 && errno != EEXIST)) return false;
    slot_file(cur, sizeof(cur), id, "game.json");
    slot_file(old, sizeof(old), id, "game.old");
    struct stat sb;
    unlink(old);
    bool had = stat(cur, &sb) == 0;
    if (had && rename(cur, old) != 0) return false;
    if (rename(src, cur) != 0) {
        if (had) rename(old, cur);
        return false;
    }
    unlink(old);
    return write_meta(pkg, id);
}

static void rm_tree1(const char *dir)   /* xoa file trong dir (1 cap) roi dir */
{
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char p[128];
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof(p), "%.63s/%.63s", dir, e->d_name);
            unlink(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

/* 4.9.7: chuyen slot cu /data/game/game.json (neu la goi Berry hop le) sang /data/games/<id>/, xoa slot cu. */
static bool s_inited;
static void ensure_init(void)
{
    ensure_mx();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (!s_inited) {
        s_inited = true;
        mkdir(GROOT, 0775);
        cJSON *j = load_json(LEGACY_DIR "/game.json", FD_GAME_PKG_MAX);
        fd_game_cfg_t c;
        if (j && parse_pkg(j, &c, true)) {
            bool ok = slot_put(LEGACY_DIR "/game.json", j, c.id);
            ESP_LOGI(TAG, "chuyen slot cu %s %s: %s", c.id, c.version, ok ? "ok" : "loi");
        }
        cJSON_Delete(j);
        rm_tree1(LEGACY_DIR);
    }
    xSemaphoreGive(s_mx);
}

bool fd_game_load(const char *id, fd_game_cfg_t *out)
{
    fd_game_cfg_t c;
    fd_game_cfg_default(&c);
    bool ok = false;
    if (id && safe_id(id)) {
        ensure_init();
        char path[80];
        slot_file(path, sizeof(path), id, "meta.json");
        cJSON *j = load_json(path, 16 * 1024);
        ok = j && parse_pkg(j, &c, false) && !strcmp(c.id, id);
        cJSON_Delete(j);
        if (!ok) {   /* meta hong / thieu: doc goi day du */
            slot_file(path, sizeof(path), id, "game.json");
            j = load_json(path, FD_GAME_PKG_MAX);
            ok = j && parse_pkg(j, &c, true) && !strcmp(c.id, id);
            if (ok) write_meta(j, id);
            cJSON_Delete(j);
        }
    }
    if (out) *out = c;
    return ok;
}

static int cfg_name_cmp(const void *a, const void *b)
{
    return strcmp(((const fd_game_cfg_t *)a)->name, ((const fd_game_cfg_t *)b)->name);
}

int fd_game_installed_list(fd_game_cfg_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    ensure_init();
    int n = 0;
    DIR *d = opendir(GROOT);
    if (!d) return 0;
    struct dirent *e;
    while (n < cap && (e = readdir(d)) != NULL) {
        if (!safe_id(e->d_name)) continue;   /* bo ".", "..", list.json */
        if (fd_game_load(e->d_name, &out[n])) n++;
    }
    closedir(d);
    qsort(out, (size_t)n, sizeof(out[0]), cfg_name_cmp);
    return n;
}

char *fd_game_script(const char *id, size_t *len)
{
    if (len) *len = 0;
    if (!id || !safe_id(id)) return NULL;
    ensure_init();
    char path[80];
    slot_file(path, sizeof(path), id, "game.json");
    cJSON *j = load_json(path, FD_GAME_PKG_MAX);
    fd_game_cfg_t c;
    char *out = NULL;
    if (j && parse_pkg(j, &c, true) && !strcmp(c.id, id)) {
        const char *sc = jstr(j, "script");
        size_t n = strlen(sc);
        out = ps_calloc(n + 1);
        if (out) {
            memcpy(out, sc, n);
            if (len) *len = n;
        }
    }
    cJSON_Delete(j);
    return out;
}

void fd_game_reset(void)
{
    ensure_init();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    DIR *d = opendir(GROOT);
    if (d) {
        struct dirent *e;
        char p[96];
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof(p), GROOT "/%.63s", e->d_name);
            struct stat sb;
            if (stat(p, &sb) == 0 && S_ISDIR(sb.st_mode)) rm_tree1(p);
            else unlink(p);
        }
        closedir(d);
    }
    rm_tree1(LEGACY_DIR);
    unlink(DL_TMP);
    unlink(LIST_TMP);
    if (s_list) memset(s_list, 0, sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
    xSemaphoreGive(s_mx);
    taskENTER_CRITICAL(&s_mux);
    s_st.n = 0;
    if (!s_busy) { s_st.state = FD_GAME_IDLE; s_st.err[0] = 0; }
    taskEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "reset: da xoa moi tro choi va cache danh sach");
}

/* ---------- danh sach ---------- */
/* JSON danh sach may chu -> muc hop le. Tra so muc, -1 neu sai dinh dang. */
static int parse_list(const char *buf, size_t len, fd_game_entry_t *tmp, int cap)
{
    cJSON *j = buf ? cJSON_ParseWithLength(buf, len) : NULL;
    const cJSON *g = j ? cJSON_GetObjectItemCaseSensitive(j, "games") : NULL;
    int n = -1;
    if (cJSON_IsArray(g)) {
        n = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, g) {
            if (n >= cap) break;
            const char *id = jstr(it, "id"), *pth = jstr(it, "path"), *sha = jstr(it, "sha256");
            if (!safe_id(id) || !safe_path(pth) || !is_hex64(sha)) continue;
            fd_game_entry_t *e = &tmp[n++];
            memset(e, 0, sizeof(*e));
            ucpy(e->id, sizeof(e->id), id);
            ucpy(e->name, sizeof(e->name), jstr(it, "name")[0] ? jstr(it, "name") : id);
            ucpy(e->version, sizeof(e->version), jstr(it, "version"));
            ucpy(e->min_fw, sizeof(e->min_fw), jstr(it, "min_fw"));
            ucpy(e->path, sizeof(e->path), pth);
            icon_cpy(e->icon, jstr(it, "icon"));
            e->berry = !strcmp(jstr(it, "type"), "berry");
            for (int k = 0; k < 64; k++) e->sha256[k] = (char)((sha[k] >= 'A' && sha[k] <= 'F') ? sha[k] + 32 : sha[k]);
            e->sha256[64] = 0;
            e->size = clampi(jint(it, "size", 0), 0, FD_GAME_PKG_MAX + 1);
            e->supported = e->berry && fw_ok(e->min_fw) && e->size <= FD_GAME_PKG_MAX;
        }
    }
    cJSON_Delete(j);
    return n;
}

int fd_game_list_cached(fd_game_entry_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    ensure_init();
    size_t len = 0;
    char *buf = read_all(LIST_CACHE, LIST_MAX, &len);
    int n = buf ? parse_list(buf, len, out, cap) : 0;
    free(buf);
    return n < 0 ? 0 : n;
}

static void list_task(void *arg)
{
    (void)arg;
    ensure_init();
    char fw[16], path[112], err[24] = "", did[24] = "";
    fw_semver(fw, sizeof(fw));
    fd_ota_get_device_id(did, sizeof(did));
    snprintf(path, sizeof(path), "/api/devices/games?hw=" HW "&fw=%s&device_id=%s", fw, did);
    int code = fd_ota_api_get_file_q(path, LIST_TMP, LIST_MAX, NULL, NULL);
    ESP_LOGI(TAG, "list: HTTP %d", code);
    int n = 0;
    fd_game_entry_t *tmp = ps_calloc(sizeof(fd_game_entry_t) * FD_GAME_LIST_MAX);
    if (code != 200) {
        http_err(err, sizeof(err), code);
    } else if (!tmp) {
        strlcpy(err, "nomem", sizeof(err));
    } else {
        size_t len = 0;
        char *buf = read_all(LIST_TMP, LIST_MAX, &len);
        n = parse_list(buf, len, tmp, FD_GAME_LIST_MAX);
        free(buf);
        if (n < 0) { n = 0; strlcpy(err, "format", sizeof(err)); }
    }
    /* 4.9.7: giu ban moi nhat lam cache (mo man Tro choi hien ngay, khong mang van co danh sach) */
    if (!err[0]) {
        unlink(LIST_CACHE);
        if (rename(LIST_TMP, LIST_CACHE) != 0) ESP_LOGW(TAG, "khong luu duoc cache danh sach");
    }
    unlink(LIST_TMP);
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
    cJSON *j = NULL;
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
        j = got ? cJSON_ParseWithLength(buf, len) : NULL;
        free(buf);
        fd_game_cfg_t c;
        if (!j) strlcpy(err, got ? "format" : "nomem", sizeof(err));
        else if (!fw_ok(jstr(j, "min_fw"))) strlcpy(err, "min_fw", sizeof(err));
        else if (!parse_pkg(j, &c, true) || strcmp(c.id, e->id) != 0) strlcpy(err, "format", sizeof(err));
    }
    /* 4.9.7: chi thay slot cua game nay (/data/games/<id>/), cac game khac giu nguyen */
    if (!err[0]) {
        ensure_init();
        if (!slot_put(DL_TMP, j, e->id)) strlcpy(err, "fs", sizeof(err));
    }
    cJSON_Delete(j);
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
