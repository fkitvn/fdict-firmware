/* fd_ota: OTA tu API may chu Fdict (CONFIG_FD_OTA_BASE_URL, xem include/fd_ota.h).
 * Log tag fd_ota, INFO, moi buoc 1 dong. KHONG BAO GIO log token (Authorization) hay query cua URL tai. */
#include "fd_ota.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fd_batt.h"
#include "fd_events.h"
#include "fd_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#ifndef FD_BUILD_PREFIX
#define FD_BUILD_PREFIX "fdictdm10"
#endif

static const char *TAG = "fd_ota";

/* Server chon luc chay theo che do nha phat trien (NVS fd/devmode): bat = Lab (http LAN), tat = chinh thuc (https + cert bundle).
 * Moi request doc lai qua fd_ota_base_url() -> doi server ap dung ngay request ke tiep. */
static volatile bool s_lab;
static volatile bool s_abort;            /* doi server: huy tai OTA dang chay */
static volatile bool s_switch_pending;   /* doi server: coi nhu chua ghep cho toi khi task xoa token */
static volatile uint32_t s_srv_gen;
const char *fd_ota_base_url(void) { return s_lab ? CONFIG_FD_OTA_LAB_BASE_URL : CONFIG_FD_OTA_BASE_URL; }
const char *fd_ota_pair_web_url(void) { return s_lab ? CONFIG_FD_PAIR_LAB_WEB_URL : CONFIG_FD_PAIR_WEB_URL; }
bool fd_ota_is_lab(void) { return s_lab; }
uint32_t fd_ota_server_gen(void) { return s_srv_gen; }
#define FD_OTA_BASE fd_ota_base_url()
#define FD_OTA_CHANNEL "stable"                  /* query channel luon 'stable' */
#define TOK_NS  "fdict"
#define TOK_KEY "device_token"
#define PAIR_POLL_MS 5000
#define PAIR_RETRY_MS 5000   /* backoff khi pair/start loi trong luc tu mo phien moi */

#define NVS_NS          "ota"
#define HTTP_TIMEOUT_MS 15000
#define REPORT_TO_MS    6000
#define RANGE_CHUNK     (64 * 1024)
#define DL_ATTEMPTS     4
#define MIN_BATT        30
#define IDLE_POLL_MS    1000
#define SELFTEST_WIFI_MS 60000
#define CHECK_MIN_S     3600
#define CHECK_MAX_S     172800
#define RETRY_ERR_S     3600
#define REPORT_RETRY_S  120
#define RQ_MAX          6

enum { CMD_CHECK = 1, CMD_UPDATE, CMD_PAIR_START, CMD_PAIR_STOP, CMD_UNPAIR, CMD_SWITCH, CMD_REBOOT };
typedef enum { RS_DOWNLOADING, RS_DOWNLOADED, RS_VERIFY_FAILED, RS_FLASHED, RS_BOOTED_OK, RS_ROLLED_BACK, RS_FAILED } rstat_t;
static const char *const RSTAT[] = {"downloading", "downloaded", "verify_failed", "flashed", "booted_ok", "rolled_back", "failed"};

typedef struct {
    char rel[40];
    char from[20];
    char to[20];
    uint8_t status;
    char err[23];
    uint32_t dur_ms;
} rep_t;

typedef struct {        /* NVS ota/pend: ban da ghi + dat boot, cho khoi dong lai / xac nhan */
    char rel[40];
    char from[20];
    char to[20];
    char part[17];       /* nhan phan vung dich (ota_0/ota_1) */
} pend_t;

typedef struct {
    bool update;
    char rel[40];
    char ver[24];
    char build[20];
    char sha[65];
    char url[256];
    uint32_t size;
    bool mandatory;
    int min_batt;
    int again_s;
    char notes[160];
} manifest_t;

static QueueHandle_t s_cmd;
static SemaphoreHandle_t s_mx;
static fd_ota_status_t s_st;
static manifest_t s_man;           /* chi task fd_ota doc/ghi */
static bool s_have_man, s_auto_pending, s_reboot_pending;
static bool (*s_is_idle)(void);
static void (*s_before_reboot)(void);
static char s_dev_id[13], s_build[20];
static volatile bool s_selftest_pending;
static bool s_display_ok, s_storage_ok;

/* ---------- trang thai cho UI ---------- */
static void st_set(fd_ota_state_t st, int pct, const char *err)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_st.state = st;
    s_st.pct = pct;
    strlcpy(s_st.err, err ? err : "", sizeof(s_st.err));
    xSemaphoreGive(s_mx);
}

void fd_ota_get_status(fd_ota_status_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_mx);
}

void fd_ota_set_hooks(bool (*is_idle)(void), void (*before_reboot)(void))
{
    s_is_idle = is_idle;
    s_before_reboot = before_reboot;
}

static void send_cmd(int c)
{
    if (s_cmd && xQueueSend(s_cmd, &c, 0) != pdTRUE) ESP_LOGI(TAG, "cmd %d: hang doi day", c);
}
void fd_ota_check_now(void) { send_cmd(CMD_CHECK); }
void fd_ota_update_now(void) { send_cmd(CMD_UPDATE); }
void fd_ota_reboot_now(void) { send_cmd(CMD_REBOOT); }

/* ---------- NVS ---------- */
void fd_ota_get_channel(char *out, size_t len) { strlcpy(out, FD_OTA_CHANNEL, len); }

/* device_token (NVS fdict/device_token, tu ghep doi): KHONG BAO GIO log / hien thi */
static bool token_get(char *out, size_t len)
{
    bool ok = false;
    nvs_handle_t h;
    if (nvs_open(TOK_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = len;
        ok = nvs_get_str(h, TOK_KEY, out, &n) == ESP_OK && out[0];
        nvs_close(h);
    }
    if (!ok && len) out[0] = 0;
    return ok;
}

static bool token_save(const char *t)   /* NULL = xoa */
{
    nvs_handle_t h;
    if (nvs_open(TOK_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = t ? nvs_set_str(h, TOK_KEY, t) : nvs_erase_key(h, TOK_KEY);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

/* Ten tai khoan da ghep (NVS fdict/user_name, khong bi mat). "" = chua co. */
#define NAME_KEY "user_name"
static void name_save(const char *n)   /* NULL/"" = xoa */
{
    nvs_handle_t h;
    if (nvs_open(TOK_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (n && n[0]) nvs_set_str(h, NAME_KEY, n);
    else nvs_erase_key(h, NAME_KEY);
    nvs_commit(h);
    nvs_close(h);
}

bool fd_ota_get_user_name(char *out, size_t len)
{
    if (!out || !len) return false;
    out[0] = 0;
    nvs_handle_t h;
    size_t n = len;
    bool ok = false;
    if (nvs_open(TOK_NS, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_str(h, NAME_KEY, out, &n) == ESP_OK && out[0];
        nvs_close(h);
    }
    if (!ok) out[0] = 0;
    return ok;
}

/* Ten tu phan hoi ghep (chiu loi): user.{name|display_name|full_name} hoac user_name/name o goc */
static const char *pick_name(const cJSON *r)
{
    static const char *const K[] = {"name", "display_name", "full_name"};
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(r, "user");
    for (int i = 0; cJSON_IsObject(u) && i < 3; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(u, K[i]);
        if (cJSON_IsString(v) && v->valuestring[0]) return v->valuestring;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(r, "user_name");
    if (cJSON_IsString(v) && v->valuestring[0]) return v->valuestring;
    v = cJSON_GetObjectItemCaseSensitive(r, "name");
    return (cJSON_IsString(v) && v->valuestring[0]) ? v->valuestring : NULL;
}

bool fd_ota_is_paired(void)
{
    if (s_switch_pending) return false;
    bool ok = false;
    nvs_handle_t h;
    size_t n = 0;
    if (nvs_open(TOK_NS, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_str(h, TOK_KEY, NULL, &n) == ESP_OK && n > 1;   /* chi do dai */
        nvs_close(h);
    }
    return ok;
}

static bool blob_get(const char *key, void *buf, size_t *len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_blob(h, key, buf, len) == ESP_OK;
    nvs_close(h);
    return ok;
}

static void blob_set(const char *key, const void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (buf && len) nvs_set_blob(h, key, buf, len);
    else nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
}

static void str_set(const char *key, const char *v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, v);
    nvs_commit(h);
    nvs_close(h);
}

static bool str_get(const char *key, char *out, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = len;
    bool ok = nvs_get_str(h, key, out, &n) == ESP_OK;
    nvs_close(h);
    return ok;
}

/* ---------- URL / HTTP ---------- */
static bool url_allowed(const char *u)
{
    if (!strncmp(u, "https://", 8)) return true;
    if (s_lab && !strncmp(u, "http://", 7)) return true;   /* chi server Lab (che do nha phat trien) */
    return false;
}

void fd_ota_get_base(char *out, size_t len, bool *lab)
{
    strlcpy(out, FD_OTA_BASE, len);
    if (lab) *lab = s_lab;
}

/* log URL chi phan duong dan: bo scheme + host (khong lo may chu) va query (URL ky co the chua chu ky) */
static void log_url(const char *what, const char *u)
{
    const char *h = strstr(u, "://");
    if (h) { const char *s = strchr(h + 3, '/'); u = s ? s : "/"; }
    const char *q = strchr(u, '?');
    ESP_LOGI(TAG, "%s: %.*s%s", what, (int)(q ? q - u : (int)strlen(u)), u, q ? "?..." : "");
}

static void http_cfg(esp_http_client_config_t *c, const char *url, int timeout_ms)
{
    memset(c, 0, sizeof(*c));
    c->url = url;
    c->timeout_ms = timeout_ms;
    c->crt_bundle_attach = esp_crt_bundle_attach;
    c->buffer_size = 4096;
    c->buffer_size_tx = 1024;
    c->max_redirection_count = 1;
    c->keep_alive_enable = true;
}

static void add_auth(esp_http_client_handle_t cl)
{
    char tok[320];
    if (!token_get(tok, sizeof(tok))) return;
    char hv[336];
    snprintf(hv, sizeof(hv), "Bearer %s", tok);
    esp_http_client_set_header(cl, "Authorization", hv);
    memset(tok, 0, sizeof(tok));
    memset(hv, 0, sizeof(hv));
}

static void pair_set(fd_pair_state_t st, bool new_session, int exp_s, const char *err);

/* HTTP 401 tu check/download/report: token khong hop le -> xoa token + ten, ve Chua ghep (khong log token) */
static bool auth_401(int code)
{
    if (code != 401 || !fd_ota_is_paired()) return false;
    bool ok = token_save(NULL);
    name_save(NULL);
    pair_set(FD_PAIR_IDLE, false, 0, NULL);
    ESP_LOGI(TAG, "auth: HTTP 401 -> token khong hop le, xoa token: %s, ve chua ghep", ok ? "OK" : "LOI");
    return true;
}

typedef struct {
    char *buf;
    int cap, len;
    char loc[256];
    bool json;         /* Content-Type: application/json */
    int retry_after;   /* header Retry-After (giay), 0 = khong co */
} rx_t;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    rx_t *rx = e->user_data;
    if (!rx) return ESP_OK;
    if (e->event_id == HTTP_EVENT_ON_DATA && rx->buf) {
        int n = e->data_len;
        if (rx->len + n > rx->cap - 1) n = rx->cap - 1 - rx->len;
        if (n > 0) { memcpy(rx->buf + rx->len, e->data, n); rx->len += n; rx->buf[rx->len] = 0; }
    } else if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && !strcasecmp(e->header_key, "Location")) {
        strlcpy(rx->loc, e->header_value, sizeof(rx->loc));
    } else if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
        if (!strcasecmp(e->header_key, "Content-Type")) rx->json = strstr(e->header_value, "application/json") != NULL;
        else if (!strcasecmp(e->header_key, "Retry-After")) rx->retry_after = atoi(e->header_value);
    }
    return ESP_OK;
}

/* ---------- 23/10: thong bao loi tu may chu ----------
 * HTTP loi (>= 400) + body JSON: CHI doc truong "message" (khong con truong ma loi, khong so khop noi dung);
 * thieu / khong doc duoc -> cau mac dinh. Luu cho UI (fd_ota_server_msg). Log ma HTTP + message (khong co token). */
#define SRV_MSG_FALLBACK "M\xC3\xA1y kh\xC3\xB4ng th\xE1\xBB\xB1" "c hi\xE1\xBB\x87n \xC4\x91\xC6\xB0\xE1\xBB\xA3" "c. Th\xE1\xBB\xAD l\xE1\xBA\xA1i sau."
static char s_srv_msg[200];
static int s_srv_code;
static uint32_t s_srv_gen_msg;
static int64_t s_srv_msg_us;

static int srv_error(int code, const rx_t *rx)
{
    if (code < 400) return 0;
    char m[200] = "";
    int ra = rx ? rx->retry_after : 0;
    if (rx && rx->json && rx->buf && rx->len > 0) {
        cJSON *j = cJSON_ParseWithLength(rx->buf, rx->len);
        const cJSON *v = j ? cJSON_GetObjectItemCaseSensitive(j, "message") : NULL;
        if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) strlcpy(m, v->valuestring, sizeof(m));
        v = j ? cJSON_GetObjectItemCaseSensitive(j, "retry_after") : NULL;
        if (cJSON_IsNumber(v) && v->valuedouble > 0) ra = (int)v->valuedouble;
        cJSON_Delete(j);
    }
    if (!m[0]) strlcpy(m, SRV_MSG_FALLBACK, sizeof(m));
    if (s_mx) {
        int64_t now = esp_timer_get_time();
        xSemaphoreTake(s_mx, portMAX_DELAY);
        /* cung ma + cung cau trong 30 s (vd poll 5 s): khong hien lai */
        if (code != s_srv_code || strcmp(m, s_srv_msg) != 0 || now - s_srv_msg_us > 30000000) {
            strlcpy(s_srv_msg, m, sizeof(s_srv_msg));
            s_srv_code = code;
            s_srv_gen_msg++;
            s_srv_msg_us = now;
        }
        xSemaphoreGive(s_mx);
    }
    ESP_LOGI(TAG, "server HTTP %d: %s%s", code, m, ra > 0 ? " (retry_after)" : "");
    return ra;
}

uint32_t fd_ota_server_msg(char *out, size_t n, int *code)
{
    if (!s_mx) { if (out && n) out[0] = 0; return 0; }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (out && n) strlcpy(out, s_srv_msg, n);
    if (code) *code = s_srv_code;
    uint32_t g = s_srv_gen_msg;
    xSemaphoreGive(s_mx);
    return g;
}

static int clamp_ra(int s) { return s < 1 ? 5 : s > 60 ? 60 : s; }   /* 429: cho toi da 60 s / lan */
#define RETRY_429_MAX 2

/* ---------- bao cao (hang doi NVS, best-effort) ---------- */
static int rq_load(rep_t *q)
{
    size_t len = sizeof(rep_t) * RQ_MAX;
    if (!blob_get("rq", q, &len) || len % sizeof(rep_t)) return 0;
    return (int)(len / sizeof(rep_t));
}

static void rq_save(const rep_t *q, int n) { blob_set("rq", n ? q : NULL, n * sizeof(rep_t)); }

static void report_queue(const char *rel, const char *from, const char *to, rstat_t st, const char *err, uint32_t dur)
{
    rep_t q[RQ_MAX];
    int n = rq_load(q);
    if (n == RQ_MAX) { memmove(q, q + 1, sizeof(rep_t) * (RQ_MAX - 1)); n--; }   /* bo ban cu nhat */
    rep_t *r = &q[n++];
    memset(r, 0, sizeof(*r));
    strlcpy(r->rel, rel, sizeof(r->rel));
    strlcpy(r->from, from, sizeof(r->from));
    strlcpy(r->to, to, sizeof(r->to));
    r->status = (uint8_t)st;
    strlcpy(r->err, err ? err : "", sizeof(r->err));
    r->dur_ms = dur;
    rq_save(q, n);
    ESP_LOGI(TAG, "report: xep %s (%s)%s%s", RSTAT[st], rel, err ? " err=" : "", err ? err : "");
}

/* gui het hang doi; dung o loi mang dau tien. true = da gui het */
static bool report_flush(void)
{
    rep_t q[RQ_MAX];
    int n = rq_load(q);
    if (!n) return true;
    if (!fd_net_is_connected()) return false;
    char url[160];
    snprintf(url, sizeof(url), "%s/api/ota/report", FD_OTA_BASE);
    if (!url_allowed(url)) { ESP_LOGI(TAG, "report: base URL khong cho phep"); return false; }
    int sent = 0;
    for (; sent < n; sent++) {
        rep_t *r = &q[sent];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "device_id", s_dev_id);
        cJSON_AddStringToObject(j, "release_id", r->rel);
        cJSON_AddStringToObject(j, "from_build", r->from);
        cJSON_AddStringToObject(j, "to_build", r->to);
        cJSON_AddStringToObject(j, "status", r->status < RS_FAILED + 1 ? RSTAT[r->status] : "failed");
        if (r->err[0]) cJSON_AddStringToObject(j, "error_code", r->err);
        if (r->dur_ms) cJSON_AddNumberToObject(j, "duration_ms", r->dur_ms);
        char *body = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
        if (!body) break;
        esp_http_client_config_t c;
        http_cfg(&c, url, REPORT_TO_MS);
        c.method = HTTP_METHOD_POST;
        char rb[512];
        rx_t rrx = {.buf = rb, .cap = sizeof(rb)};
        rb[0] = 0;
        c.event_handler = http_evt;
        c.user_data = &rrx;
        esp_http_client_handle_t cl = esp_http_client_init(&c);
        int code = -1;
        esp_err_t e = ESP_FAIL;
        if (cl) {
            add_auth(cl);
            esp_http_client_set_header(cl, "Content-Type", "application/json");
            esp_http_client_set_post_field(cl, body, strlen(body));
            e = esp_http_client_perform(cl);
            code = esp_http_client_get_status_code(cl);
            esp_http_client_cleanup(cl);
        }
        free(body);
        ESP_LOGI(TAG, "report: POST %s -> %s %d", RSTAT[r->status < RS_FAILED + 1 ? r->status : RS_FAILED], esp_err_to_name(e), code);
        if (e == ESP_OK && code >= 400) srv_error(code, &rrx);
        if (e == ESP_OK && auth_401(code)) break;   /* token vua bi xoa: giu hang doi, gui lai sau */
        if (e != ESP_OK || code == 429 || code >= 500 || code < 0) break;   /* thu lai sau */
        /* 2xx / 4xx khac (vd trung lap): bo khoi hang doi */
    }
    if (sent) { memmove(q, q + sent, sizeof(rep_t) * (n - sent)); rq_save(q, n - sent); }
    return sent == n;
}

/* ---------- kiem tra ---------- */
static int jitter_s(int s)
{
    if (s < CHECK_MIN_S) s = CHECK_MIN_S;
    if (s > CHECK_MAX_S) s = CHECK_MAX_S;
    int j = s / 10;
    return s - j + (int)(esp_random() % (uint32_t)(2 * j + 1));
}

static void jstr(const cJSON *o, const char *k, char *out, size_t len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    out[0] = 0;
    if (cJSON_IsString(v) && v->valuestring) strlcpy(out, v->valuestring, len);
}

static bool is_hex64(const char *s)
{
    if (strlen(s) != 64) return false;
    for (; *s; s++) if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))) return false;
    return true;
}

/* tra ve so giay den lan kiem tra ke tiep */
static int do_check(bool manual)
{
    if (!fd_net_is_connected()) { st_set(FD_OTA_ST_ERROR, 0, "no_wifi"); ESP_LOGI(TAG, "check: bo qua (chua WiFi)"); return RETRY_ERR_S; }
    if (s_reboot_pending) { st_set(FD_OTA_ST_REBOOT_PENDING, 100, NULL); return CONFIG_FD_OTA_CHECK_INTERVAL_S; }
    st_set(FD_OTA_ST_CHECKING, 0, NULL);
    const char *ch = FD_OTA_CHANNEL;
    int bat = fd_batt_get_pct();
    bat = bat < 0 ? 0 : bat > 100 ? 100 : bat;
    int rssi = fd_net_rssi();
    if (rssi > 0) rssi = 0;
    char url[320];
    snprintf(url, sizeof(url), "%s/api/ota/check?hw=%s&channel=%s&fw_version=%s&build_id=%s&device_id=%s&battery=%d&rssi=%d",
             FD_OTA_BASE, CONFIG_FD_OTA_HW, ch, esp_app_get_description()->version, s_build, s_dev_id, bat, rssi);
    if (!url_allowed(url)) { st_set(FD_OTA_ST_ERROR, 0, "bad_url"); ESP_LOGI(TAG, "check: base URL khong phai https://"); return RETRY_ERR_S; }
    ESP_LOGI(TAG, "check: GET /api/ota/check (%s, channel=%s, %s, %s, token=%s)", manual ? "thu cong" : "tu dong",
             ch, s_build, s_dev_id, fd_ota_is_paired() ? "co" : "khong");
    rx_t rx = {.cap = 2048};
    rx.buf = calloc(1, rx.cap);
    if (!rx.buf) { st_set(FD_OTA_ST_ERROR, 0, "no_mem"); return RETRY_ERR_S; }
    /* Kiem tra OTA doc lap voi ghep doi: luon gui device_id (MAC); Bearer chi khi co token.
     * 401 co Bearer -> thu lai 1 lan khong Bearer. 401 khi chua ghep -> trang thai "http_401", khong phai "net". */
    esp_err_t e = ESP_FAIL;
    int code = -1;
    bool use_auth = fd_ota_is_paired();
    for (int att = 0; att < 2; att++) {
        rx.len = 0;
        rx.json = false;
        rx.retry_after = 0;
        rx.loc[0] = 0;
        esp_http_client_config_t c;
        http_cfg(&c, url, HTTP_TIMEOUT_MS);
        c.event_handler = http_evt;
        c.user_data = &rx;
        esp_http_client_handle_t cl = esp_http_client_init(&c);
        e = ESP_FAIL;
        code = -1;
        if (cl) {
            if (use_auth) add_auth(cl);
            esp_http_client_set_header(cl, "Cache-Control", "no-store");
            e = esp_http_client_perform(cl);
            code = esp_http_client_get_status_code(cl);
            esp_http_client_cleanup(cl);
        }
        ESP_LOGI(TAG, "check: %s HTTP %d, %d B (bearer=%s)", esp_err_to_name(e), code, rx.len, use_auth ? "co" : "khong");
        if (code == 401) e = ESP_OK;   /* co phan hoi 401 = khong phai loi mang */
        if (!(e == ESP_OK && code == 401 && use_auth)) break;
        use_auth = false;   /* token bi tu choi: thu lai khong Bearer (khong xoa token o day) */
    }
    int ra = (e == ESP_OK && code >= 400) ? srv_error(code, &rx) : 0;
    if (e == ESP_OK && code == 401 && !fd_ota_is_paired()) {
        st_set(FD_OTA_ST_ERROR, 0, "http_401");   /* UI: "may chu (401)", khong phai "mang" */
        ESP_LOGI(TAG, "check: HTTP 401 khi chua ghep (may chu yeu cau Bearer cho device_id nay)");
        free(rx.buf);
        return RETRY_ERR_S;
    }
    if (e == ESP_OK && code == 429) {   /* qua tai: cho retry_after roi tu kiem tra lai */
        st_set(FD_OTA_ST_ERROR, 0, "http_429");
        free(rx.buf);
        return ra > 0 ? (ra > CHECK_MAX_S ? CHECK_MAX_S : ra) : RETRY_ERR_S;
    }
    if (e != ESP_OK || code != 200) {
        char er[20];
        if (e != ESP_OK) strlcpy(er, "net", sizeof(er)); else snprintf(er, sizeof(er), "http_%d", code);
        st_set(FD_OTA_ST_ERROR, 0, er);
        free(rx.buf);
        return RETRY_ERR_S;
    }
    cJSON *j = cJSON_ParseWithLength(rx.buf, rx.len);
    free(rx.buf);
    if (!j) { st_set(FD_OTA_ST_ERROR, 0, "bad_json"); ESP_LOGI(TAG, "check: JSON loi"); return RETRY_ERR_S; }
    manifest_t m = {0};
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "check_again_s");
    m.again_s = cJSON_IsNumber(v) ? (int)v->valuedouble : CONFIG_FD_OTA_CHECK_INTERVAL_S;
    m.update = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "update"));
    if (m.update) {
        jstr(j, "release_id", m.rel, sizeof(m.rel));
        jstr(j, "fw_version", m.ver, sizeof(m.ver));
        jstr(j, "build_id", m.build, sizeof(m.build));
        jstr(j, "sha256", m.sha, sizeof(m.sha));
        jstr(j, "url", m.url, sizeof(m.url));   /* dung nguyen URL tu may chu, khong dung lai host */
        jstr(j, "notes_vi", m.notes, sizeof(m.notes));
        v = cJSON_GetObjectItemCaseSensitive(j, "size");
        m.size = cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint32_t)v->valuedouble : 0;
        m.mandatory = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "mandatory"));
        v = cJSON_GetObjectItemCaseSensitive(j, "min_battery");
        m.min_batt = cJSON_IsNumber(v) ? (int)v->valuedouble : MIN_BATT;
    }
    cJSON_Delete(j);
    int next = jitter_s(m.again_s);
    if (!m.update) {
        s_have_man = s_auto_pending = false;
        st_set(FD_OTA_ST_UP_TO_DATE, 0, NULL);
        ESP_LOGI(TAG, "check: da la ban moi nhat, kiem tra lai sau %d s", next);
        return next;
    }
    if (!m.rel[0] || !m.ver[0] || !is_hex64(m.sha) || !m.size || !url_allowed(m.url)) {
        st_set(FD_OTA_ST_ERROR, 0, "bad_manifest");
        ESP_LOGI(TAG, "check: manifest thieu/sai truong (rel/ver/sha256/size/url)");
        return RETRY_ERR_S;
    }
    char bad[40] = "";
    str_get("bad_rel", bad, sizeof(bad));
    s_man = m;
    s_have_man = true;
    s_auto_pending = strcmp(bad, m.rel) != 0;   /* da rollback ban nay tren may -> khong tu dong tai lai */
    xSemaphoreTake(s_mx, portMAX_DELAY);
    strlcpy(s_st.version, m.ver, sizeof(s_st.version));
    strlcpy(s_st.build, m.build, sizeof(s_st.build));
    strlcpy(s_st.notes, m.notes, sizeof(s_st.notes));
    s_st.size = m.size;
    s_st.mandatory = m.mandatory;
    xSemaphoreGive(s_mx);
    st_set(FD_OTA_ST_AVAILABLE, 0, NULL);
    ESP_LOGI(TAG, "check: co ban moi %s %s (%u B, rel %s, mandatory=%d, min_battery=%d)%s", m.ver, m.build,
             (unsigned)m.size, m.rel, m.mandatory, m.min_batt, s_auto_pending ? "" : " - da rollback, chi tai thu cong");
    return next;
}

/* ---------- tai + xac minh ---------- */
static bool batt_ok(int need)
{
    if (need < MIN_BATT) need = MIN_BATT;
    int p = fd_batt_get_pct();
    if (p < 0) return true;   /* khong doc duoc pin (tat FD_BATTERY_ENABLE): khong chan */
    return p >= need;         /* fd_power chua co tin hieu dang sac */
}

static esp_err_t sha_partition(const esp_partition_t *p, uint32_t size, char out[65])
{
    psa_crypto_init();
    uint8_t *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(4096);
    if (!buf) return ESP_ERR_NO_MEM;
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    esp_err_t r = psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
    for (uint32_t off = 0; r == ESP_OK && off < size; off += 4096) {
        uint32_t n = size - off < 4096 ? size - off : 4096;
        r = esp_partition_read(p, off, buf, n);
        if (r == ESP_OK && psa_hash_update(&op, buf, n) != PSA_SUCCESS) r = ESP_FAIL;
    }
    uint8_t h[32];
    size_t hl = 0;
    if (r == ESP_OK && (psa_hash_finish(&op, h, sizeof(h), &hl) != PSA_SUCCESS || hl != 32)) r = ESP_FAIL;
    if (r != ESP_OK) psa_hash_abort(&op);
    free(buf);
    if (r == ESP_OK) for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
    return r;
}

static esp_err_t auth_init_cb(esp_http_client_handle_t cl) { add_auth(cl); return ESP_OK; }

/* 24/10: may chu (download) bat buoc ?device_id=<12 hex> khi co Bearer; URL tu /api/ota/check khong co -> tu them.
 * Da co device_id -> giu nguyen. Khong vua bo dem -> log canh bao, giu URL goc. */
static void url_add_dev_id(char *u, size_t cap)
{
    if (!s_dev_id[0] || strstr(u, "?device_id=") || strstr(u, "&device_id=")) return;
    size_t n = strlen(u);
    if (n >= cap) return;
    int k = snprintf(u + n, cap - n, "%cdevice_id=%s", strchr(u, '?') ? '&' : '?', s_dev_id);
    if (k < 0 || (size_t)k >= cap - n) {
        u[n] = 0;
        ESP_LOGW(TAG, "download: URL qua dai, khong them device_id");
        return;
    }
    ESP_LOGI(TAG, "download: them device_id vao URL tai");
}

/* b cung host voi a va la duong dan /api/ota/download/ (khong ap cho URL ky san S3) */
static bool same_host_download(const char *a, const char *b)
{
    const char *ha = strstr(a, "://"), *hb = strstr(b, "://");
    if (!ha || !hb) return false;
    ha += 3;
    hb += 3;
    size_t la = strcspn(ha, "/?#"), lb = strcspn(hb, "/?#");
    if (la != lb || strncasecmp(ha, hb, la)) return false;
    return !strncmp(hb + lb, "/api/ota/download/", 18);
}

/* URL tai: neu may chu chuyen huong 1 lan (302 toi URL ky san) -> dung Location va KHONG gui token toi host khac */
static bool resolve_url(const char *url, char *out, size_t len, bool *with_auth)
{
    strlcpy(out, url, len);
    *with_auth = true;
    rx_t rx = {0};
    esp_http_client_config_t c;
    http_cfg(&c, url, HTTP_TIMEOUT_MS);
    c.disable_auto_redirect = true;
    c.event_handler = http_evt;
    c.user_data = &rx;
    esp_http_client_handle_t cl = esp_http_client_init(&c);
    if (!cl) return false;
    add_auth(cl);
    esp_http_client_set_header(cl, "Range", "bytes=0-0");
    esp_err_t e = esp_http_client_perform(cl);
    int code = esp_http_client_get_status_code(cl);
    esp_http_client_cleanup(cl);
    ESP_LOGI(TAG, "download: probe %s HTTP %d", esp_err_to_name(e), code);
    if (e == ESP_OK) auth_401(code);
    if (e != ESP_OK && !(code >= 300 && code < 400)) return false;
    if (code >= 300 && code < 400) {
        if (!rx.loc[0] || !url_allowed(rx.loc)) { ESP_LOGI(TAG, "download: redirect khong hop le"); return false; }
        strlcpy(out, rx.loc, len);
        *with_auth = same_host_download(url, out);   /* cung host /api/ota/download: giu token + device_id */
        if (*with_auth) url_add_dev_id(out, len);
        log_url("download: redirect ->", out);
    }
    return code < 400;
}

static void fail(const char *code, rstat_t rs, uint32_t dur)
{
    report_queue(s_man.rel, s_build, s_man.build, rs, code, dur);
    st_set(FD_OTA_ST_ERROR, 0, code);
    report_flush();
}

static void do_update(bool manual)
{
    if (!s_have_man) { ESP_LOGI(TAG, "update: chua co manifest"); return; }
    if (!fd_net_is_connected()) { st_set(FD_OTA_ST_ERROR, 0, "no_wifi"); return; }
    if (!batt_ok(s_man.min_batt)) {
        ESP_LOGI(TAG, "update: pin %d%% < %d%% -> cho", fd_batt_get_pct(), s_man.min_batt > MIN_BATT ? s_man.min_batt : MIN_BATT);
        if (manual) st_set(FD_OTA_ST_ERROR, 0, "batt_low");
        return;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { st_set(FD_OTA_ST_ERROR, 0, "no_part"); return; }
    if (s_man.size >= part->size) {
        ESP_LOGI(TAG, "update: %u B >= phan vung %s %u B", (unsigned)s_man.size, part->label, (unsigned)part->size);
        s_auto_pending = false;
        fail("too_large", RS_FAILED, 0);
        return;
    }
    s_auto_pending = false;   /* moi lan tai chi 1 lan tu dong; loi -> lan kiem tra sau */
    int64_t t0 = esp_timer_get_time();
    ESP_LOGI(TAG, "update: bat dau %s -> %s (%s) vao %s, %s", s_build, s_man.build, s_man.ver, part->label, manual ? "thu cong" : "tu dong");
    st_set(FD_OTA_ST_DOWNLOADING, 0, NULL);
    report_queue(s_man.rel, s_build, s_man.build, RS_DOWNLOADING, NULL, 0);
    report_flush();

    char url[256];
    bool with_auth = true;
    char src[256];
    strlcpy(src, s_man.url, sizeof(src));
    url_add_dev_id(src, sizeof(src));   /* luon gui device_id (ca khi chua ghep) */
    if (!resolve_url(src, url, sizeof(url), &with_auth)) { fail("net", RS_FAILED, 0); return; }

    esp_http_client_config_t hc;
    http_cfg(&hc, url, HTTP_TIMEOUT_MS);
    esp_https_ota_config_t oc = {
        .http_config = &hc,
        .http_client_init_cb = with_auth ? auth_init_cb : NULL,
        .partial_http_download = true,
        .max_http_request_size = RANGE_CHUNK,
        .buffer_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
    };
    esp_https_ota_handle_t h = NULL;
    esp_err_t e = ESP_FAIL;
    int written = 0, last_pct = -1;
    for (int att = 1; att <= DL_ATTEMPTS; att++) {
        oc.ota_resumption = written > 0;
        oc.ota_image_bytes_written = written;
        e = esp_https_ota_begin(&oc, &h);
        ESP_LOGI(TAG, "download: begin #%d tu %d B: %s", att, written, esp_err_to_name(e));
        if (e == ESP_OK) {
            int sz = esp_https_ota_get_image_size(h);
            if (sz > 0 && (uint32_t)sz != s_man.size) {
                ESP_LOGI(TAG, "download: Content-Length %d != manifest %u", sz, (unsigned)s_man.size);
                esp_https_ota_abort(h);
                fail("size_mismatch", RS_FAILED, 0);
                return;
            }
            while ((e = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                if (s_abort) {   /* doi server: bo anh dang tai (esp_https_ota_abort, khong set boot, khong bao cao) */
                    esp_https_ota_abort(h);
                    ESP_LOGI(TAG, "download: huy (doi server)");
                    st_set(FD_OTA_ST_IDLE, 0, NULL);
                    return;
                }
                int rd = esp_https_ota_get_image_len_read(h);
                int pct = (int)((int64_t)rd * 100 / s_man.size);
                if (pct != last_pct) {
                    st_set(FD_OTA_ST_DOWNLOADING, pct > 99 ? 99 : pct, NULL);
                    if (pct / 10 != last_pct / 10) ESP_LOGD(TAG, "download: %d%% (%d B)", pct, rd);
                    last_pct = pct;
                }
            }
            if (e == ESP_OK && esp_https_ota_is_complete_data_received(h)) break;
            written = esp_https_ota_get_image_len_read(h);
            ESP_LOGI(TAG, "download: dut o %d B: %s", written, esp_err_to_name(e));
            esp_https_ota_abort(h);
            h = NULL;
        }
        if (s_abort) { ESP_LOGI(TAG, "download: huy (doi server)"); st_set(FD_OTA_ST_IDLE, 0, NULL); return; }
        if (att < DL_ATTEMPTS) vTaskDelay(pdMS_TO_TICKS(3000 * att));
        if (!fd_net_is_connected() && !fd_net_wait_connected(30000)) break;
    }
    if (s_abort) { if (h) esp_https_ota_abort(h); st_set(FD_OTA_ST_IDLE, 0, NULL); return; }
    if (!h) { fail("net", RS_FAILED, (uint32_t)((esp_timer_get_time() - t0) / 1000)); return; }
    uint32_t dur = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "download: xong %u B trong %u ms", (unsigned)s_man.size, (unsigned)dur);

    /* xac minh: SHA256 toan bo .bin doc lai tu flash + esp_app_desc */
    char sha[65];
    e = sha_partition(part, s_man.size, sha);
    bool sha_ok = e == ESP_OK && !strcasecmp(sha, s_man.sha);
    ESP_LOGI(TAG, "verify: sha256 %.12s... vs manifest %.12s...: %s", e == ESP_OK ? sha : "?", s_man.sha, sha_ok ? "OK" : "SAI");
    if (!sha_ok) { esp_https_ota_abort(h); fail("verify_failed", RS_VERIFY_FAILED, dur); return; }   /* bo anh, khong reboot */
    report_queue(s_man.rel, s_build, s_man.build, RS_DOWNLOADED, NULL, dur);
    esp_app_desc_t nd;
    const esp_app_desc_t *cur = esp_app_get_description();
    e = esp_ota_get_partition_description(part, &nd);
    bool desc_ok = e == ESP_OK && !strncmp(nd.project_name, cur->project_name, sizeof(nd.project_name)) &&
                   !strncmp(nd.version, "FDICT-", 6) && !strncmp(nd.version, s_man.ver, sizeof(nd.version));
    ESP_LOGI(TAG, "verify: app '%.32s' %.32s: %s", e == ESP_OK ? nd.project_name : "?", e == ESP_OK ? nd.version : "?", desc_ok ? "OK" : "SAI");
    if (!desc_ok) { esp_https_ota_abort(h); fail("bad_image", RS_FAILED, dur); return; }
    if (s_abort) { esp_https_ota_abort(h); ESP_LOGI(TAG, "download: huy (doi server)"); st_set(FD_OTA_ST_IDLE, 0, NULL); return; }

    e = esp_https_ota_finish(h);   /* esp_ota_end (kiem anh) + esp_ota_set_boot_partition */
    ESP_LOGI(TAG, "flash: finish + set boot %s: %s", part->label, esp_err_to_name(e));
    if (e != ESP_OK) { fail(e == ESP_ERR_OTA_VALIDATE_FAILED ? "bad_image" : "flash", RS_FAILED, dur); return; }
    pend_t p = {0};
    strlcpy(p.rel, s_man.rel, sizeof(p.rel));
    strlcpy(p.from, s_build, sizeof(p.from));
    strlcpy(p.to, s_man.build, sizeof(p.to));
    strlcpy(p.part, part->label, sizeof(p.part));
    blob_set("pend", &p, sizeof(p));
    report_queue(s_man.rel, s_build, s_man.build, RS_FLASHED, NULL, (uint32_t)((esp_timer_get_time() - t0) / 1000));
    report_flush();
    s_have_man = false;
    s_reboot_pending = true;
    st_set(FD_OTA_ST_REBOOT_PENDING, 100, NULL);
    ESP_LOGI(TAG, "reboot: cho thiet bi ranh");
}

/* ---------- tu kiem tra sau OTA / phat hien rollback ---------- */
static void boot_state(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(run, &st);
    pend_t p;
    size_t pl = sizeof(p);
    bool have = blob_get("pend", &p, &pl) && pl == sizeof(p);
    ESP_LOGI(TAG, "boot: %s state=%d, pending=%s", run->label, (int)st, have ? p.rel : "-");
    if (st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_selftest_pending = true;
        return;   /* task fd_ota chay tu kiem tra */
    }
    if (!have) return;
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    if (strcmp(run->label, p.part) != 0 && bad && !strcmp(bad->label, p.part)) {
        ESP_LOGI(TAG, "boot: ban %s (%s) da rollback -> bao rolled_back", p.to, p.part);
        report_queue(p.rel, p.from, p.to, RS_ROLLED_BACK, "selftest", 0);
        str_set("bad_rel", p.rel);
    } else {
        ESP_LOGI(TAG, "boot: xoa pending (%s)", strcmp(run->label, p.part) ? "chua chay ban moi" : "da hop le");
    }
    blob_set("pend", NULL, 0);
}

static void selftest_run(void)
{
    if (!s_selftest_pending) return;
    bool wifi = s_display_ok && s_storage_ok && fd_net_wait_connected(SELFTEST_WIFI_MS);
    ESP_LOGI(TAG, "selftest: display=%d storage=%d wifi=%d", s_display_ok, s_storage_ok, wifi);
    if (!wifi) {
        ESP_LOGI(TAG, "selftest: THAT BAI -> rollback + khoi dong lai");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "selftest: OK -> mark valid: %s", esp_err_to_name(e));
    pend_t p;
    size_t pl = sizeof(p);
    if (blob_get("pend", &p, &pl) && pl == sizeof(p)) {
        report_queue(p.rel, p.from, p.to, RS_BOOTED_OK, NULL, 0);
        blob_set("pend", NULL, 0);
    }
    s_selftest_pending = false;
}

void fd_ota_selftest_wait(uint32_t timeout_ms)
{
    for (uint32_t t = 0; s_selftest_pending && t < timeout_ms; t += 100) vTaskDelay(pdMS_TO_TICKS(100));
}

/* ---------- ghep doi (POST /api/devices/pair/start, /poll moi 5 s, 410 -> phien moi) ---------- */
static fd_pair_status_t s_pair;      /* doc qua fd_ota_pair_get (s_mx) */
static bool s_pair_active;
static int64_t s_pair_exp_us, s_pair_next_us;

static void pair_set(fd_pair_state_t st, bool new_session, int exp_s, const char *err)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_pair.state = st;
    if (new_session) s_pair.session++;
    s_pair.expires_s = exp_s;
    strlcpy(s_pair.err, err ? err : "", sizeof(s_pair.err));
    xSemaphoreGive(s_mx);
}

void fd_ota_pair_get(fd_pair_status_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    *out = s_pair;
    if (out->state == FD_PAIR_CODE) {
        int64_t left = (s_pair_exp_us - esp_timer_get_time()) / 1000000;
        out->expires_s = left < 0 ? 0 : (int)left;
    }
    xSemaphoreGive(s_mx);
    out->paired = fd_ota_is_paired();
}
void fd_ota_pair_start(void) { send_cmd(CMD_PAIR_START); }
void fd_ota_pair_stop(void) { send_cmd(CMD_PAIR_STOP); }
void fd_ota_unpair(void) { send_cmd(CMD_UNPAIR); }

/* Doi server (bat/tat che do nha phat trien): ap dung URL ngay; task fd_ota huy ghep + tai OTA, xoa hang doi bao cao, kiem tra lai. */
void fd_ota_set_server(bool lab)
{
    s_switch_pending = true;
    s_abort = true;
    s_lab = lab;
    s_srv_gen++;
    if (s_mx) pair_set(FD_PAIR_IDLE, false, 0, NULL);
    int c = CMD_SWITCH;
    if (s_cmd && xQueueSendToFront(s_cmd, &c, pdMS_TO_TICKS(200)) != pdTRUE) ESP_LOGI(TAG, "cmd switch: hang doi day");
}

/* POST JSON; tra HTTP status (-1 loi mang). Khong log body (co the chua token). */
static int post_json(const char *path, cJSON *body, rx_t *rx)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", FD_OTA_BASE, path);
    if (!url_allowed(url)) return -1;
    char *b = cJSON_PrintUnformatted(body);
    if (!b) return -1;
    int code = -1;
    for (int att = 0; att <= RETRY_429_MAX; att++) {
        if (rx) { rx->len = 0; rx->json = false; rx->retry_after = 0; if (rx->buf) rx->buf[0] = 0; }
        esp_http_client_config_t c;
        http_cfg(&c, url, HTTP_TIMEOUT_MS);
        c.method = HTTP_METHOD_POST;
        c.event_handler = http_evt;
        c.user_data = rx;
        esp_http_client_handle_t cl = esp_http_client_init(&c);
        code = -1;
        if (cl) {
            add_auth(cl);
            esp_http_client_set_header(cl, "Content-Type", "application/json");
            esp_http_client_set_post_field(cl, b, strlen(b));
            if (esp_http_client_perform(cl) == ESP_OK) code = esp_http_client_get_status_code(cl);
            esp_http_client_cleanup(cl);
        }
        int ra = srv_error(code, rx);
        if (code != 429 || att == RETRY_429_MAX) break;
        ESP_LOGI(TAG, "POST %s 429 -> cho %d s roi thu lai (%d/%d)", path, clamp_ra(ra), att + 1, RETRY_429_MAX);
        vTaskDelay(pdMS_TO_TICKS(clamp_ra(ra) * 1000));
    }
    free(b);
    return code;
}

/* API dung chung (fd_study, fd_deck): POST JSON co Bearer token; 401 -> xoa token (auth_401). Goi tu task khac (khong phai LVGL).
 * Giu 1 ket noi HTTPS keep-alive dung lai giua cac lan goi (bo bat tay TLS ~1-2 s moi lan); mutex: 1 request/luc.
 * Ket noi cu bi server dong -> mo lai 1 lan. Tra HTTP status (-1 loi mang / URL khong hop le). KHONG log token / header / body. */
static SemaphoreHandle_t s_api_mx;
static esp_http_client_handle_t s_api_cl;
static char s_api_base[128];

static void api_drop(void)
{
    if (s_api_cl) { esp_http_client_cleanup(s_api_cl); s_api_cl = NULL; }
}

static int api_once(const char *url, const char *json, rx_t *rx, bool *reused)
{
    *reused = s_api_cl != NULL;
    if (!s_api_cl) {
        esp_http_client_config_t c;
        http_cfg(&c, url, HTTP_TIMEOUT_MS);
        c.method = HTTP_METHOD_POST;
        c.event_handler = http_evt;
        c.user_data = rx;
        s_api_cl = esp_http_client_init(&c);
        if (!s_api_cl) return -1;
        strlcpy(s_api_base, FD_OTA_BASE, sizeof(s_api_base));
    } else {
        esp_http_client_set_url(s_api_cl, url);
        esp_http_client_set_method(s_api_cl, HTTP_METHOD_POST);
        esp_http_client_set_user_data(s_api_cl, rx);
    }
    if (fd_ota_is_paired()) add_auth(s_api_cl);
    else esp_http_client_delete_header(s_api_cl, "Authorization");
    esp_http_client_set_header(s_api_cl, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_api_cl, json, (int)strlen(json));
    int code = -1;
    if (esp_http_client_perform(s_api_cl) == ESP_OK) code = esp_http_client_get_status_code(s_api_cl);
    else api_drop();   /* loi mang / ket noi da dong: lan sau mo moi */
    return code;
}

int fd_ota_api_post_ex(const char *path, const char *json, char *resp, int cap, bool retry_429)
{
    if (resp && cap > 0) resp[0] = 0;
    if (!path || !json || !fd_net_is_connected()) return -1;
    rx_t rx = {.buf = (resp && cap > 1) ? resp : NULL, .cap = cap, .len = 0};
    char url[160];
    snprintf(url, sizeof(url), "%s%s", FD_OTA_BASE, path);
    if (!url_allowed(url)) return -1;
    char eb[512];   /* body loi khi caller khong cap buffer: van doc duoc "message" */
    if (!rx.buf) { rx.buf = eb; rx.cap = sizeof(eb); }
    if (!s_api_mx) {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        SemaphoreHandle_t m = xSemaphoreCreateMutex();
        taskENTER_CRITICAL(&mux);
        if (!s_api_mx) { s_api_mx = m; m = NULL; }
        taskEXIT_CRITICAL(&mux);
        if (m) vSemaphoreDelete(m);
        if (!s_api_mx) return -1;
    }
    int64_t t0 = esp_timer_get_time();
    xSemaphoreTake(s_api_mx, portMAX_DELAY);
    int64_t t_wait = esp_timer_get_time() - t0;
    if (s_api_cl && strcmp(s_api_base, FD_OTA_BASE)) api_drop();   /* doi server (che do nha phat trien) */
    int code = -1;
    bool reused = false;
    for (int att = 0; att <= RETRY_429_MAX; att++) {
        rx.len = 0; rx.json = false; rx.retry_after = 0; rx.buf[0] = 0;
        code = api_once(url, json, &rx, &reused);
        if (code < 0 && reused) {   /* keep-alive da bi server dong: thu lai ngay voi ket noi moi */
            rx.len = 0; rx.json = false; rx.retry_after = 0; rx.buf[0] = 0;
            code = api_once(url, json, &rx, &reused);
        }
        int ra = srv_error(code, &rx);
        if (code != 429 || !retry_429 || att == RETRY_429_MAX) break;
        ESP_LOGI(TAG, "api POST %s 429 -> cho %d s roi thu lai (%d/%d)", path, clamp_ra(ra), att + 1, RETRY_429_MAX);
        vTaskDelay(pdMS_TO_TICKS(clamp_ra(ra) * 1000));
    }
    if (code == 401) api_drop();
    xSemaphoreGive(s_api_mx);
    ESP_LOGD(TAG, "api POST %s -> %d (%d ms%s%s)", path, code, (int)((esp_timer_get_time() - t0) / 1000),
             reused ? ", keep-alive" : ", ket noi moi", t_wait > 50000 ? ", cho mutex" : "");
    if (code == 401 && s_mx) auth_401(code);
    return code;
}

int fd_ota_api_post(const char *path, const char *json, char *resp, int cap)
{
    return fd_ota_api_post_ex(path, json, resp, cap, true);
}

/* GET co ETag / Range: bat su kien header de lay ETag (khong log header nao). */
typedef struct {
    char *etag;
    size_t cap;
    bool json;
    int retry_after;
} gx_t;

static esp_err_t gx_evt(esp_http_client_event_t *e)
{
    gx_t *g = e->user_data;
    if (g && g->etag && e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && !strcasecmp(e->header_key, "ETag"))
        strlcpy(g->etag, e->header_value ? e->header_value : "", g->cap);
    else if (g && e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
        if (!strcasecmp(e->header_key, "Content-Type")) g->json = strstr(e->header_value, "application/json") != NULL;
        else if (!strcasecmp(e->header_key, "Retry-After")) g->retry_after = atoi(e->header_value);
    }
    return ESP_OK;
}

/* GET {base}{path} + Bearer (khong log token/header) -> file dst.
 * resume: dst da co N byte -> Range: bytes=N- (+ If-Range: etag_in); 206 -> ghi tiep, 200 -> ghi lai tu dau.
 * !resume && etag_in: If-None-Match -> 304 (khong ghi). etag_out: ETag phan hoi. prog(got, total) moi ~16 KB.
 * Tra HTTP status; -1 mang/URL, -2 qua max_bytes, -3 loi file/bo nho. 401 -> xoa token (auth_401).
 * Loi giua chung: giu phan da ghi (de noi tiep) khi resume, nguoc lai xoa dst. */
int fd_ota_api_get_file_ex(const char *path, const char *dst, int max_bytes, bool resume, const char *etag_in,
                           char *etag_out, size_t etag_cap, void (*prog)(int got, int total, void *ctx), void *ctx)
{
    if (etag_out && etag_cap) etag_out[0] = 0;
    if (!path || !dst || max_bytes <= 0 || !fd_net_is_connected()) return -1;
    char url[200];
    snprintf(url, sizeof(url), "%s%s", FD_OTA_BASE, path);
    if (!url_allowed(url)) return -1;
    long off = 0;
    if (resume) {
        FILE *pf = fopen(dst, "rb");
        if (pf) { if (fseek(pf, 0, SEEK_END) == 0) off = ftell(pf); fclose(pf); }
        if (off < 0) off = 0;
    }
    gx_t g = {.etag = etag_out, .cap = etag_cap};
    esp_http_client_config_t c;
    http_cfg(&c, url, HTTP_TIMEOUT_MS);
    c.method = HTTP_METHOD_GET;
    c.event_handler = gx_evt;
    c.user_data = &g;
    esp_http_client_handle_t cl = esp_http_client_init(&c);
    if (!cl) return -1;
    add_auth(cl);
    char rh[40];
    if (off > 0) {
        snprintf(rh, sizeof(rh), "bytes=%ld-", off);
        esp_http_client_set_header(cl, "Range", rh);
        if (etag_in && etag_in[0]) esp_http_client_set_header(cl, "If-Range", etag_in);
    } else if (etag_in && etag_in[0]) {
        esp_http_client_set_header(cl, "If-None-Match", etag_in);
    }
    int code = -1, tot = 0, total = -1;
    bool keep_part = false;
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        int64_t len = esp_http_client_fetch_headers(cl);
        code = esp_http_client_get_status_code(cl);
        bool append = code == 206 && off > 0;
        if (code == 200 || append) {
            long base = append ? off : 0;
            total = len >= 0 ? (int)(base + len) : -1;
            if (total > max_bytes) code = -2;
        }
        if (code == 200 || code == 206) {
            if (code == 206 && !append) code = -1;   /* 206 khong mong doi */
        }
        if (code == 200 || (code == 206 && append)) {
            int ok_code = code;
            long base = append ? off : 0;
            FILE *f = fopen(dst, append ? "ab" : "wb");
            char *buf = f ? malloc(4096) : NULL;
            if (!buf) code = -3;
            int last = 0;
            while (buf) {
                int r = esp_http_client_read(cl, buf, 4096);
                if (r < 0) { code = -1; break; }
                if (r == 0) { if (!esp_http_client_is_complete_data_received(cl)) code = -1; break; }
                tot += r;
                if (base + tot > max_bytes) { code = -2; break; }
                if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) { code = -3; break; }
                if (prog && tot - last >= 16384) { last = tot; prog((int)(base + tot), total, ctx); }
            }
            free(buf);
            if (f) fclose(f);
            if (code == ok_code) {
                if (prog) prog((int)(base + tot), total, ctx);
            } else if (resume && code == -1) {
                keep_part = true;   /* mat ket noi: giu de noi tiep */
            }
            if (code != ok_code && !keep_part) unlink(dst);
        } else if (code == 416) {
            unlink(dst);   /* phan da co khong hop le: tai lai tu dau (noi bo resume, khong hien thong bao) */
        } else if (code >= 400) {   /* loi: chi doc "message" neu body JSON */
            char eb[512];
            rx_t erx = {.buf = eb, .cap = sizeof(eb), .json = g.json, .retry_after = g.retry_after};
            if (g.json) { int r = esp_http_client_read(cl, eb, sizeof(eb) - 1); erx.len = r > 0 ? r : 0; }
            eb[erx.len] = 0;
            srv_error(code, &erx);
        }
    }
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    ESP_LOGD(TAG, "api GET %s -> %d (tu %ld, +%d B%s)", path, code, off, tot, keep_part ? ", giu phan da tai" : "");
    if (code == 401 && s_mx) auth_401(code);
    return code;
}

/* Ban don gian (fd_voice): khong ETag / Range. */
int fd_ota_api_get_file(const char *path, const char *dst, int max_bytes)
{
    return fd_ota_api_get_file_ex(path, dst, max_bytes, false, NULL, NULL, 0, NULL, NULL);
}

void fd_ota_get_device_id(char *out, size_t len)
{
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(out, len, "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static int64_t s_pair_retry_us;   /* >0: tu thu lai pair/start (sau 410/het han) */
static void pair_request(void)
{
    if (!fd_net_is_connected()) { pair_set(FD_PAIR_ERROR, false, 0, "no_wifi"); s_pair_active = false; return; }
    pair_set(FD_PAIR_REQUESTING, false, 0, NULL);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "device_id", s_dev_id);
    cJSON_AddStringToObject(j, "hw", CONFIG_FD_OTA_HW);
    cJSON_AddStringToObject(j, "fw_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(j, "build_id", s_build);
    rx_t rx = {.cap = 1024};
    rx.buf = calloc(1, rx.cap);
    int code = rx.buf ? post_json("/api/devices/pair/start", j, &rx) : -1;
    cJSON_Delete(j);
    cJSON *r = (code >= 200 && code < 300) ? cJSON_ParseWithLength(rx.buf, rx.len) : NULL;
    free(rx.buf);
    int exp = 600;   /* pair_code (neu server con tra) bi bo qua: khong hien, khong dua vao QR */
    bool ok = r != NULL;
    if (r) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(r, "expires_in");
        if (cJSON_IsNumber(v) && v->valuedouble > 0) exp = (int)v->valuedouble;
        cJSON_Delete(r);
    }
    ESP_LOGI(TAG, "pair: start HTTP %d, het han %d s", code, exp);
    if (!ok) {
        char er[20];
        if (code < 0) strlcpy(er, "net", sizeof(er)); else snprintf(er, sizeof(er), "http_%d", code);
        if (s_pair_retry_us) {   /* dang tu mo phien moi: giu active, thu lai sau 5 s (khong loop nhanh) */
            pair_set(FD_PAIR_REQUESTING, false, 0, NULL);
            s_pair_retry_us = esp_timer_get_time() + (int64_t)PAIR_RETRY_MS * 1000;
            return;
        }
        pair_set(FD_PAIR_ERROR, false, 0, er);
        s_pair_active = false;
        return;
    }
    s_pair_retry_us = 0;
    int64_t now = esp_timer_get_time();
    s_pair_exp_us = now + (int64_t)exp * 1000000;
    s_pair_next_us = now + (int64_t)PAIR_POLL_MS * 1000;
    pair_set(FD_PAIR_CODE, true, exp, NULL);
}

static void pair_poll(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "device_id", s_dev_id);
    rx_t rx = {.cap = 1024};
    rx.buf = calloc(1, rx.cap);
    int code = rx.buf ? post_json("/api/devices/pair/poll", j, &rx) : -1;
    cJSON_Delete(j);
    cJSON *r = (code >= 200 && code < 300) ? cJSON_ParseWithLength(rx.buf, rx.len) : NULL;
    if (rx.buf) { memset(rx.buf, 0, rx.cap); free(rx.buf); }   /* body co the chua token */
    if (code == 410) {   /* phien het han -> tu mo phien moi + QR moi */
        ESP_LOGI(TAG, "pair: poll 410 het han -> mo phien moi");
        s_pair_retry_us = 1;
        pair_request();
        return;
    }
    if (!r) {
        static int64_t last;
        int64_t now = esp_timer_get_time();
        if (now - last > 30000000) { ESP_LOGD(TAG, "pair: poll HTTP %d", code); last = now; }
        return;   /* loi tam thoi: thu lai 5 s sau den khi het han */
    }
    bool paired = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "paired"));
    bool issued = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "already_issued"));
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(r, "device_token");
    if (paired && cJSON_IsString(t) && t->valuestring && t->valuestring[0]) {
        bool ok = token_save(t->valuestring);
        memset(t->valuestring, 0, strlen(t->valuestring));
        const char *nm = pick_name(r);
        char nb[64] = "";
        if (nm) strlcpy(nb, nm, sizeof(nb));
        name_save(ok ? nb : NULL);
        ESP_LOGI(TAG, "pair: da ghep, luu token: %s, ten tai khoan: %s", ok ? "OK" : "LOI", nb[0] ? "co" : "khong co");
        s_pair_active = false;
        pair_set(ok ? FD_PAIR_DONE : FD_PAIR_ERROR, false, 0, ok ? NULL : "nvs");
    } else if (paired && issued && fd_ota_is_paired()) {   /* da co token trong NVS -> coi nhu da ghep */
        ESP_LOGI(TAG, "pair: token da cap truoc do, NVS da co token -> da ghep");
        s_pair_active = false;
        pair_set(FD_PAIR_DONE, false, 0, NULL);
    } else if (paired && issued) {
        ESP_LOGI(TAG, "pair: token da cap truoc do -> xoa token, ghep lai");
        token_save(NULL);
        name_save(NULL);
        pair_request();
    } else if (!paired) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(r, "expires_in");
        if (cJSON_IsNumber(v) && v->valuedouble >= 0) s_pair_exp_us = esp_timer_get_time() + (int64_t)v->valuedouble * 1000000;
    }
    cJSON_Delete(r);
}

static void pair_tick(void)
{
    if (!s_pair_active) return;
    int64_t now = esp_timer_get_time();
    if (s_pair_retry_us > 1) { if (now >= s_pair_retry_us) pair_request(); return; }
    if (now >= s_pair_exp_us) { ESP_LOGI(TAG, "pair: phien het han -> mo phien moi"); s_pair_retry_us = 1; pair_request(); return; }
    if (now >= s_pair_next_us) { s_pair_next_us = now + (int64_t)PAIR_POLL_MS * 1000; pair_poll(); }
}

/* ---------- task ---------- */
static bool idle_now(void) { return s_is_idle ? s_is_idle() : false; }

static void ota_task(void *arg)
{
    (void)arg;
    selftest_run();
    int64_t next_check = 0;            /* 0 = chua len lich (cho WiFi + gio) */
    int64_t next_report = 0;
    for (;;) {
        int cmd = 0;
        xQueueReceive(s_cmd, &cmd, pdMS_TO_TICKS(s_pair_active ? 250 : IDLE_POLL_MS));
        if (cmd == CMD_PAIR_START) { s_pair_active = true; s_pair_retry_us = 0; pair_request(); cmd = 0; }
        else if (cmd == CMD_UNPAIR) {   /* huy ghep: dung poll + xoa NVS fdict/device_token */
            s_pair_active = false;
            bool ok = token_save(NULL);
            name_save(NULL);
            pair_set(FD_PAIR_IDLE, false, 0, NULL);
            ESP_LOGI(TAG, "pair: huy ghep, xoa token: %s", ok ? "OK" : "LOI");
            cmd = 0;
        }
        else if (cmd == CMD_SWITCH) {   /* doi server: huy ghep + bo manifest/bao cao cua server cu, kiem tra lai sau 5 s */
            s_pair_active = false;
            s_pair_retry_us = 0;
            token_save(NULL);
            name_save(NULL);
            rq_save(NULL, 0);
            s_have_man = s_auto_pending = false;
            s_abort = false;
            s_switch_pending = false;
            pair_set(FD_PAIR_IDLE, false, 0, NULL);
            st_set(FD_OTA_ST_IDLE, 0, NULL);
            ESP_LOGI(TAG, "server switched to %s, unpaired", s_lab ? "lab" : "prod");
            if (fd_net_is_connected()) next_check = esp_timer_get_time() + 5LL * 1000000;
            else next_check = 0;
            cmd = 0;
        }
        else if (cmd == CMD_PAIR_STOP) {
            if (s_pair_active) { s_pair_active = false; pair_set(FD_PAIR_IDLE, false, 0, NULL); ESP_LOGI(TAG, "pair: dung"); }
            cmd = 0;
        }
        pair_tick();
        int64_t now = esp_timer_get_time();
        bool net = fd_net_is_connected();   /* khong can dong bo gio (SNTP) */
        if (!next_check && net) {
            int d = 30 + (int)(esp_random() % 91);
            next_check = now + (int64_t)d * 1000000;
            ESP_LOGI(TAG, "schedule: WiFi OK -> kiem tra sau %d s", d);
        }
        if (cmd == CMD_CHECK) {
            int s = do_check(true);
            next_check = now + (int64_t)s * 1000000;
        } else if (cmd == CMD_UPDATE) {
            if (s_have_man && !s_reboot_pending) do_update(true);
        } else if (next_check && now >= next_check) {
            int s = net ? do_check(false) : 60;
            next_check = esp_timer_get_time() + (int64_t)s * 1000000;
        }
        if (s_have_man && s_auto_pending && !s_reboot_pending && net && idle_now()) do_update(false);
        if (s_reboot_pending && (cmd == CMD_REBOOT || idle_now())) {   /* nut "Khoi dong lai ngay" hoac thiet bi ranh */
            ESP_LOGI(TAG, "reboot: %s -> khoi dong lai de cap nhat", cmd == CMD_REBOOT ? "nguoi dung bam" : "thiet bi ranh");
            if (s_before_reboot) s_before_reboot();
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
        if (net && now >= next_report) {
            if (!report_flush()) next_report = now + (int64_t)REPORT_RETRY_S * 1000000;
            else next_report = now + (int64_t)60 * 1000000;
        }
    }
}

esp_err_t fd_ota_start(bool display_ok, bool storage_ok)
{
    if (s_cmd) return ESP_OK;
    s_display_ok = display_ok;
    s_storage_ok = storage_ok;
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(s_dev_id, sizeof(s_dev_id), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    char sha[9];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    snprintf(s_build, sizeof(s_build), FD_BUILD_PREFIX "%.4s", sha);
    {
        nvs_handle_t h;
        uint8_t v = 0;   /* mac dinh TAT che do nha phat trien khi NVS chua co khoa */
        if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "devmode", &v); nvs_close(h); }
        s_lab = v != 0;   /* che do nha phat trien -> server Lab */
    }
    s_mx = xSemaphoreCreateMutex();
    s_cmd = xQueueCreate(6, sizeof(int));
    if (!s_mx || !s_cmd) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "start: device_id=%s build=%s hw=%s channel=%s server=%s token=%s", s_dev_id, s_build, CONFIG_FD_OTA_HW,
             FD_OTA_CHANNEL, s_lab ? "lab" : "prod", fd_ota_is_paired() ? "co" : "khong");
    boot_state();
    if (xTaskCreatePinnedToCore(ota_task, "fd_ota", 8192, NULL, 3, NULL, tskNO_AFFINITY) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
