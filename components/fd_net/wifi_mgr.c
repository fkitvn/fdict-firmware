/*
 * fd_net / wifi_mgr: WiFi STA, quet, luu nhieu mang (NVS), tu ket noi lai co backoff.
 * Mat khau KHONG bao gio duoc log.
 */
#include "esp_mac.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "fd_events.h"
#include "fd_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "fd_net";

#define NVS_NS        "wifi"
#define NVS_KEY       "nets"
#define BIT_CONNECTED BIT0
#define BIT_SCAN_DONE BIT1
#define RETRY_BASE_MS 1500   /* thu lai mang moi: 1.5 s + jitter 0..500 ms */
#define RETRY_JITTER_MS 500
#define POST_SCAN_MS  200    /* cho radio on dinh sau quet */
#define MODE_SLOTS    8
#define BACKOFF_MAX_S 60

typedef struct {
    char ssid[33];
    char pass[65];
} saved_net_t;

void fd_net_time_start(void);   /* net_time.c */

static SemaphoreHandle_t s_mtx;
static EventGroupHandle_t s_eg;
static esp_netif_t *s_netif;
static esp_timer_handle_t s_retry_timer;

static saved_net_t s_saved[FD_NET_MAX_SAVED];
static int s_saved_n;

static fd_net_ap_t s_scan[FD_NET_MAX_SCAN];
static int s_scan_n;
static uint8_t s_scan_bssid[FD_NET_MAX_SCAN][6];   /* AP manh nhat cua moi SSID */
static uint8_t s_scan_ch[FD_NET_MAX_SCAN];
static uint8_t s_scan_auth[FD_NET_MAX_SCAN];
static volatile bool s_scanning;

static volatile fd_net_state_t s_state = FD_NET_IDLE;
static saved_net_t s_target;        /* mang dang thu */
static bool s_target_new;           /* nguoi dung vua nhap, chua luu */
static int s_new_attempts;
static int s_backoff_n;
static int s_rr;                    /* round-robin trong danh sach da luu */
static volatile bool s_self_disconnect;
static char s_ip[16];
/* Bo dem chi de doc (man Debug), khong anh huong hanh vi */
static volatile uint32_t s_dbg_disc_n;
static volatile int s_dbg_last_reason;
static volatile int64_t s_dbg_last_us, s_dbg_next_retry_us;

/* LOG: moi AP (BSSID) khop SSID muc tieu/da luu trong lan quet gan nhat (on_scan_done chi giu 1 AP / SSID) */
#define CAND_MAX FD_NET_MAX_SCAN   /* moi BSSID trong lan quet (de tra kenh/authmode theo BSSID) */
static struct { char ssid[33]; uint8_t bssid[6]; uint8_t ch; int8_t rssi; uint8_t auth; } s_cand[CAND_MAX];
static int s_cand_n;

static void log_sta_mac(const char *where)
{
    uint8_t m[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, m) == ESP_OK) {
        ESP_LOGI(TAG, "STA MAC (%s): " MACSTR, where, MAC2STR(m));
    }
}

static const char *h2e_name(int v)
{
    switch (v) {
    case WPA3_SAE_PWE_UNSPECIFIED: return "UNSPECIFIED";
    case WPA3_SAE_PWE_HUNT_AND_PECK: return "HUNT_AND_PECK";
    case WPA3_SAE_PWE_HASH_TO_ELEMENT: return "H2E";
    case WPA3_SAE_PWE_BOTH: return "BOTH";
    default: return "?";
    }
}

/* Ten buoc that bai (tieng Viet khong dau) theo reason */
static const char *step_name(int r)
{
    switch (r) {
    case WIFI_REASON_AUTH_EXPIRE: return "buoc auth: khong phan hoi (AP khong tra loi frame auth)";
    case WIFI_REASON_AUTH_FAIL: return "buoc auth: bi tu choi / SAE that bai";
    case WIFI_REASON_ASSOC_FAIL: return "buoc assoc: that bai";
    case WIFI_REASON_ASSOC_TOOMANY: return "buoc assoc: router day client";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "buoc 4-way handshake: het gio (thuong la sai mat khau)";
    case WIFI_REASON_MIC_FAILURE: return "buoc 4-way handshake: sai MIC (sai mat khau)";
    case WIFI_REASON_CONNECTION_FAIL: return "ket noi that bai (AP tu choi/khong tra loi)";
    case WIFI_REASON_BEACON_TIMEOUT: return "da ket noi, mat beacon (song yeu/AP tat)";
    case WIFI_REASON_SA_QUERY_TIMEOUT: return "PMF: SA query het gio";
    case WIFI_REASON_ASSOC_LEAVE:
    case WIFI_REASON_AUTH_LEAVE: return "AP/STA chu dong ngat";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "buoc quet: khong thay AP phu hop";
    default: return "khac";
    }
}

/* Kieu thu lai dang cho tren s_retry_timer */
typedef enum { RETRY_SAVED = 0, RETRY_TARGET } retry_kind_t;
static retry_kind_t s_retry_kind;
static bool s_compat;               /* che do cua lan ket noi dang chay: false = thuong, true = WPA2-compat */

/* Che do (thuong / WPA2-compat) se dung cho tung SSID, chi luu RAM */
static struct { char ssid[33]; bool compat; bool ok; } s_mode[MODE_SLOTS];
static int s_mode_next;

static int mode_slot(const char *ssid, bool create)
{
    for (int i = 0; i < MODE_SLOTS; i++) {
        if (s_mode[i].ssid[0] && strcmp(s_mode[i].ssid, ssid) == 0) return i;
    }
    if (!create) return -1;
    int i = s_mode_next;
    s_mode_next = (s_mode_next + 1) % MODE_SLOTS;
    strlcpy(s_mode[i].ssid, ssid, sizeof(s_mode[i].ssid));
    s_mode[i].compat = false;
    s_mode[i].ok = false;
    return i;
}

static bool mode_get(const char *ssid)
{
    int i = mode_slot(ssid, false);
    return i >= 0 && s_mode[i].compat;
}

static const char *mode_name(bool compat) { return compat ? "WPA2-compat" : "thuong"; }

static const char *reason_name(int r)
{
    switch (r) {
    case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    default: return "?";
    }
}

/* Hen gio ket noi tren s_retry_timer (moi ket noi lai deu di qua day, khong goi thang tu event handler) */
static void schedule_ms(retry_kind_t kind, uint32_t ms)
{
    s_retry_kind = kind;
    s_state = FD_NET_WAIT_RETRY;
    esp_timer_stop(s_retry_timer);
    s_dbg_next_retry_us = esp_timer_get_time() + (int64_t)ms * 1000;
    esp_timer_start_once(s_retry_timer, (uint64_t)ms * 1000ULL);
}

/* ---------- NVS ---------- */
static void saved_load(void)
{
    nvs_handle_t h;
    s_saved_n = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_saved);
    if (nvs_get_blob(h, NVS_KEY, s_saved, &len) == ESP_OK) {
        s_saved_n = (int)(len / sizeof(saved_net_t));
    }
    nvs_close(h);
}

static void saved_store(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (s_saved_n > 0) {
        nvs_set_blob(h, NVS_KEY, s_saved, s_saved_n * sizeof(saved_net_t));
    } else {
        nvs_erase_key(h, NVS_KEY);
    }
    nvs_commit(h);
    nvs_close(h);
}

static int saved_find(const char *ssid)
{
    for (int i = 0; i < s_saved_n; i++) {
        if (strcmp(s_saved[i].ssid, ssid) == 0) return i;
    }
    return -1;
}

/* Dua mang len dau danh sach (MRU) */
static void saved_put_front(const saved_net_t *n)
{
    int i = saved_find(n->ssid);
    if (i < 0) {
        i = (s_saved_n < FD_NET_MAX_SAVED) ? s_saved_n++ : FD_NET_MAX_SAVED - 1;
    }
    memmove(&s_saved[1], &s_saved[0], i * sizeof(saved_net_t));
    s_saved[0] = *n;
    saved_store();
}

/* ---------- ket noi ---------- */
static void do_connect(const saved_net_t *n)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, n->ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, n->pass, sizeof(cfg.sta.password));
    /* authmode AP tu lan quet gan nhat (-1 = chua thay) */
    int ap_auth = -1;
    for (int i = 0; i < s_scan_n; i++) {
        if (strcmp(s_scan[i].ssid, n->ssid) == 0) { ap_auth = s_scan_auth[i]; break; }
    }
    bool ap_wpa3 = ap_auth == WIFI_AUTH_WPA3_PSK || ap_auth == WIFI_AUTH_WPA2_WPA3_PSK;
    s_compat = n->pass[0] && mode_get(n->ssid);
    ESP_LOGD(TAG, "AP \"%s\" authmode quet duoc = %d%s, che do %s", n->ssid, ap_auth, ap_wpa3 ? " (WPA3)" : "",
             mode_name(s_compat));
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
#if CONFIG_FD_WIFI_SAE_AUTO
    if (s_compat) {
        /* WPA2-compat: chi WPA2-PSK, khong SAE, khong PMF (sau reason 2/205) */
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK;
        cfg.sta.pmf_cfg.capable = false;
    } else if (!n->pass[0]) {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else if (ap_wpa3) {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    } else {
        /* AP WPA/WPA2: khong bat SAE, cho phep WPA/WPA2 mixed */
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
        cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
    }
#else
    cfg.sta.threshold.authmode = n->pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = s_compat ? WPA3_SAE_PWE_HUNT_AND_PECK : WPA3_SAE_PWE_BOTH;
    if (s_compat) cfg.sta.pmf_cfg.capable = false;
#endif
    /* Mang moi, 2 lan thu dau: ghim BSSID + kenh cua AP manh nhat trong lan quet gan nhat
     * (SSID co nhieu AP/repeater tren nhieu kenh). Lan thu sau bo ghim de STA tu chon. */
#ifdef FD_NET_PIN_BSSID   /* TAT: nghi la nguyen nhan auth timeout (reason 2) */
    if (s_target_new && s_new_attempts < 2) {
        for (int i = 0; i < s_scan_n; i++) {
            if (strcmp(s_scan[i].ssid, n->ssid) != 0) continue;
            cfg.sta.bssid_set = true;
            memcpy(cfg.sta.bssid, s_scan_bssid[i], 6);
            cfg.sta.channel = s_scan_ch[i];
            ESP_LOGD(TAG, "Ghim AP " MACSTR " kenh %u rssi %d authmode %u", MAC2STR(s_scan_bssid[i]),
                     s_scan_ch[i], s_scan[i].rssi, s_scan_auth[i]);
            break;
        }
    }
#endif
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    s_target = *n;
    /* Khong ket noi khi dang quet: scan chiem radio, frame auth bi lo -> AUTH_EXPIRE.
     * Dung quet va hen ket noi lai sau khi SCAN_DONE (on_scan_done/timer), khong cho o day. */
    if (s_scanning) {
        ESP_LOGW(TAG, "Dang quet -> dung quet, ket noi sau SCAN_DONE + %d ms", POST_SCAN_MS);
        esp_wifi_scan_stop();
        schedule_ms(RETRY_TARGET, POST_SCAN_MS);
        return;
    }
    s_state = FD_NET_CONNECTING;
    {
        int bi = -1;
        for (int i = 0; i < s_scan_n; i++) if (strcmp(s_scan[i].ssid, n->ssid) == 0) { bi = i; break; }
        log_sta_mac("truoc ket noi");
        if (bi >= 0) {
            ESP_LOGD(TAG, "Lan thu %d mode=%s muc tieu(quet) bssid=" MACSTR " ch=%u rssi=%d authmode=%u | pmf_cap=%d pmf_req=%d h2e=%d(%s)",
                     s_target_new ? s_new_attempts + 1 : s_backoff_n + 1, mode_name(s_compat), MAC2STR(s_scan_bssid[bi]),
                     s_scan_ch[bi], s_scan[bi].rssi, s_scan_auth[bi], cfg.sta.pmf_cfg.capable, cfg.sta.pmf_cfg.required,
                     cfg.sta.sae_pwe_h2e, h2e_name(cfg.sta.sae_pwe_h2e));
        } else {
            ESP_LOGD(TAG, "Lan thu %d mode=%s muc tieu(quet)=chua thay | pmf_cap=%d pmf_req=%d h2e=%d(%s)",
                     s_target_new ? s_new_attempts + 1 : s_backoff_n + 1, mode_name(s_compat),
                     cfg.sta.pmf_cfg.capable, cfg.sta.pmf_cfg.required, cfg.sta.sae_pwe_h2e, h2e_name(cfg.sta.sae_pwe_h2e));
        }
    }
    ESP_LOGD(TAG, "sta cfg: ssid=\"%s\" mode=%s bssid_set=%d bssid=" MACSTR " ch=%u thr=%d pmf_cap=%d pmf_req=%d h2e=%d scan=%d sort=%d",
             (const char *)cfg.sta.ssid, mode_name(s_compat), cfg.sta.bssid_set, MAC2STR(cfg.sta.bssid), cfg.sta.channel,
             cfg.sta.threshold.authmode, cfg.sta.pmf_cfg.capable, cfg.sta.pmf_cfg.required,
             cfg.sta.sae_pwe_h2e, cfg.sta.scan_method, cfg.sta.sort_method);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ESP_LOGD(TAG, "Ket noi toi \"%s\"%s (mat khau %u ky tu)", n->ssid, s_target_new ? " (mang moi)" : "",
             (unsigned)strlen(n->pass));
    esp_wifi_connect();
    fd_events_post(FD_EVT_WIFI_CONNECTING, NULL, 0);
}

static void connect_next_saved(void)
{
    if (s_saved_n == 0 || s_scanning) {
        s_state = FD_NET_IDLE;
        return;
    }
    s_target_new = false;
    s_rr = s_rr % s_saved_n;
    do_connect(&s_saved[s_rr]);
    s_rr++;
}

static void schedule_retry(void)
{
    if (s_saved_n == 0) {
        s_state = FD_NET_IDLE;
        return;
    }
    int delay_s = 1 << (s_backoff_n < 6 ? s_backoff_n : 6);
    if (delay_s > BACKOFF_MAX_S) delay_s = BACKOFF_MAX_S;
    s_backoff_n++;
    ESP_LOGI(TAG, "Thu ket noi lai sau %d s", delay_s);
    schedule_ms(RETRY_SAVED, (uint32_t)delay_s * 1000U);
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_state == FD_NET_WAIT_RETRY) {   /* chi 1 ket noi tai mot thoi diem */
        if (s_scanning) {
            /* quet chua xong: doi them (on_scan_done se hen lai) */
            esp_timer_start_once(s_retry_timer, 300 * 1000ULL);
        } else if (s_retry_kind == RETRY_TARGET && s_target.ssid[0]) {
            do_connect(&s_target);
        } else {
            connect_next_saved();
        }
    }
    xSemaphoreGive(s_mtx);
}

static bool reason_is_auth_fail(int r)
{
    /* Chi cac ly do xay ra trong/sau 4-way handshake moi coi la sai mat khau.
     * AUTH_EXPIRE (2) = router khong tra loi frame auth -> KHONG phai sai mat khau. */
    return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           r == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

/* Router tu choi/khong tra loi o buoc auth/assoc */
static bool reason_is_auth_reject(int r)
{
    return r == WIFI_REASON_AUTH_EXPIRE ||
           r == WIFI_REASON_ASSOC_FAIL || r == WIFI_REASON_CONNECTION_FAIL;
}

static bool reason_is_no_ap(int r)
{
    return r == WIFI_REASON_NO_AP_FOUND || r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
           r == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD || r == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

static void on_scan_done(void)
{
    uint16_t n = FD_NET_MAX_SCAN;
    wifi_ap_record_t *rec = calloc(FD_NET_MAX_SCAN, sizeof(wifi_ap_record_t));
    if (!rec) {
        esp_wifi_clear_ap_list();
        s_scanning = false;
        xEventGroupSetBits(s_eg, BIT_SCAN_DONE);
        return;
    }
    esp_wifi_scan_get_ap_records(&n, rec);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_scan_n = 0;
    s_cand_n = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)rec[i].ssid;
        if (!ssid[0]) continue;
        if (s_cand_n < CAND_MAX) {
            strlcpy(s_cand[s_cand_n].ssid, ssid, sizeof(s_cand[0].ssid));
            memcpy(s_cand[s_cand_n].bssid, rec[i].bssid, 6);
            s_cand[s_cand_n].ch = rec[i].primary;
            s_cand[s_cand_n].rssi = rec[i].rssi;
            s_cand[s_cand_n].auth = rec[i].authmode;
            s_cand_n++;
        }
        if (strcmp(ssid, s_target.ssid) == 0 || saved_find(ssid) >= 0) {
            ESP_LOGD(TAG, "Quet: AP khop \"%s\" bssid=" MACSTR " ch=%u rssi=%d authmode=%u", ssid,
                     MAC2STR(rec[i].bssid), rec[i].primary, rec[i].rssi, rec[i].authmode);
        }
        bool dup = false;   /* bo trung SSID (nhieu AP), giu RSSI cao nhat (da sap xep) */
        for (int j = 0; j < s_scan_n; j++) {
            if (strcmp(s_scan[j].ssid, ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;
        fd_net_ap_t *a = &s_scan[s_scan_n++];
        strlcpy(a->ssid, ssid, sizeof(a->ssid));
        a->rssi = rec[i].rssi;
        a->secure = rec[i].authmode != WIFI_AUTH_OPEN;
        a->saved = saved_find(ssid) >= 0;
        memcpy(s_scan_bssid[s_scan_n - 1], rec[i].bssid, 6);
        s_scan_ch[s_scan_n - 1] = rec[i].primary;
        s_scan_auth[s_scan_n - 1] = rec[i].authmode;
    }
    s_scanning = false;
    xEventGroupSetBits(s_eg, BIT_SCAN_DONE);
    /* Tiep tuc ket noi (qua timer, cho radio on dinh POST_SCAN_MS) */
    if (s_state == FD_NET_WAIT_RETRY && s_retry_kind == RETRY_TARGET) {
        schedule_ms(RETRY_TARGET, POST_SCAN_MS);
    } else if ((s_state == FD_NET_IDLE || s_state == FD_NET_WAIT_RETRY) && s_saved_n > 0) {
        schedule_ms(RETRY_SAVED, POST_SCAN_MS);
    }
    xSemaphoreGive(s_mtx);
    free(rec);
    ESP_LOGI(TAG, "Quet xong: %d mang", s_scan_n);
    fd_events_post(FD_EVT_WIFI_SCAN_DONE, NULL, 0);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        on_scan_done();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *c = data;
        ESP_LOGD(TAG, "buoc auth+assoc: OK bssid=" MACSTR " ch=%u authmode=%d -> cho 4-way handshake/DHCP",
                 MAC2STR(c->bssid), c->channel, c->authmode);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        xEventGroupClearBits(s_eg, BIT_CONNECTED);
        s_ip[0] = 0;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (s_self_disconnect) {
            s_self_disconnect = false;
            xSemaphoreGive(s_mtx);
            return;
        }
        s_dbg_disc_n++;
        s_dbg_last_reason = d->reason;
        s_dbg_last_us = esp_timer_get_time();
        fd_evt_wifi_disc_t ev = {.reason = d->reason};
        strlcpy(ev.ssid, s_target.ssid, sizeof(ev.ssid));
        ev.no_ap = reason_is_no_ap(d->reason);
        /* ch/authmode cua DUNG BSSID trong event (truoc day lay theo SSID -> luon ra kenh AP manh nhat) */
        int sc_ch = 0, sc_auth = -1;
        for (int i = 0; i < s_cand_n; i++) {
            if (memcmp(s_cand[i].bssid, d->bssid, 6) == 0) { sc_ch = s_cand[i].ch; sc_auth = s_cand[i].auth; break; }
        }
        if (sc_auth < 0) {   /* BSSID khong co trong lan quet -> lay AP manh nhat cung SSID */
            for (int i = 0; i < s_scan_n; i++) {
                if (strcmp(s_scan[i].ssid, s_target.ssid) == 0) { sc_ch = s_scan_ch[i]; sc_auth = s_scan_auth[i]; break; }
            }
        }
        uint8_t cur_ch = 0; wifi_second_chan_t sec; esp_wifi_get_channel(&cur_ch, &sec);
        ESP_LOGD(TAG, "Mat ket noi \"%s\"", s_target.ssid);
        ESP_LOGW(TAG, "Mat ket noi reason=%d (%s) rssi=%d bssid=" MACSTR " ch=%d (kenh radio=%u) lan=%d mode=%s authmode=%d",
                 d->reason, reason_name(d->reason), d->rssi, MAC2STR(d->bssid), sc_ch, cur_ch,
                 s_target_new ? s_new_attempts + 1 : s_backoff_n + 1, mode_name(s_compat), sc_auth);
        ESP_LOGW(TAG, "That bai o: %s", step_name(d->reason));
        /* Router khong tra loi auth (2) / connection fail (205): doi che do thuong <-> WPA2-compat cho lan sau */
        if ((d->reason == WIFI_REASON_AUTH_EXPIRE || d->reason == WIFI_REASON_CONNECTION_FAIL) && s_target.pass[0]) {
            int m = mode_slot(s_target.ssid, true);
            s_mode[m].compat = !s_compat;
            ESP_LOGD(TAG, "Lan sau dung che do %s cho \"%s\"", mode_name(s_mode[m].compat), s_target.ssid);
        }
        if (s_target_new) {
            /* Mang moi: thu lai (sai mat khau: 1 lan nua; loi auth/assoc cua router: toi 4 lan), sau do bao loi */
            int max_try = reason_is_auth_fail(d->reason) ? 2 : 4;
            if (++s_new_attempts < max_try && !ev.no_ap) {
                uint32_t ms = RETRY_BASE_MS + esp_random() % (RETRY_JITTER_MS + 1);
                ESP_LOGI(TAG, "Thu lai %d/%d sau %u ms (reason=%d)", s_new_attempts, max_try - 1, (unsigned)ms, d->reason);
                schedule_ms(RETRY_TARGET, ms);
                xSemaphoreGive(s_mtx);
                return;
            }
            ev.auth_fail = reason_is_auth_fail(d->reason);
            ev.auth_reject = reason_is_auth_reject(d->reason);
            s_target_new = false;
            s_state = FD_NET_IDLE;
            if (ev.auth_fail) {
                ESP_LOGW(TAG, "Sai mat khau");
                ESP_LOGD(TAG, "Sai mat khau cho \"%s\"", ev.ssid);
            }
            schedule_retry();   /* quay ve cac mang da luu (neu co) */
        } else if (!s_scanning) {
            schedule_retry();
        } else {
            s_state = FD_NET_IDLE;
        }
        xSemaphoreGive(s_mtx);
        fd_events_post(FD_EVT_WIFI_DISCONNECTED, &ev, sizeof(ev));
    }
}

/* DNS du phong: router chi cap 1 DNS (hoac DNS loi) -> getaddrinfo EAI_FAIL. Them 1.1.1.1 / 8.8.8.8 vao o trong;
 * lwIP tu thu DNS ke tiep khi DNS chinh khong tra loi. Khong ghi de DNS do DHCP cap. */
static void dns_backup(void)
{
    static const uint32_t alt[] = { ESP_IP4TOADDR(1, 1, 1, 1), ESP_IP4TOADDR(8, 8, 8, 8) };
    const esp_netif_dns_type_t ty[] = { ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK };
    for (int i = 0; i < 2; i++) {
        esp_netif_dns_info_t d = {0};
        if (esp_netif_get_dns_info(s_netif, ty[i], &d) == ESP_OK && d.ip.u_addr.ip4.addr != 0) continue;
        d.ip.type = ESP_IPADDR_TYPE_V4;
        d.ip.u_addr.ip4.addr = alt[i];
        esp_netif_set_dns_info(s_netif, ty[i], &d);
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *e = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_target_new) {
        saved_put_front(&s_target);
        s_target_new = false;
        s_rr = 1;
    }
    s_backoff_n = 0;
    s_state = FD_NET_CONNECTED;
    if (s_target.pass[0]) {
        int m = mode_slot(s_target.ssid, true);
        s_mode[m].compat = s_compat;
        s_mode[m].ok = true;
    }
    ESP_LOGI(TAG, "Ket noi thanh cong che do %s", mode_name(s_compat));
    xSemaphoreGive(s_mtx);
    xEventGroupSetBits(s_eg, BIT_CONNECTED);
    ESP_LOGD(TAG, "Da ket noi \"%s\", IP %s", s_target.ssid, s_ip);
    dns_backup();
    fd_net_time_start();
    fd_events_post(FD_EVT_WIFI_CONNECTED, NULL, 0);
}

esp_err_t fd_net_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    s_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, "fdict");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));   /* tu quan ly danh sach trong NVS "wifi" */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL));
    const esp_timer_create_args_t targs = {.callback = retry_timer_cb, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    log_sta_mac("khoi dong");
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_err_t cc_err = esp_wifi_set_country_code("VN", true);   /* VN: kenh 1-13 */
    if (cc_err != ESP_OK) ESP_LOGW(TAG, "set_country_code VN: %s", esp_err_to_name(cc_err));
#if CONFIG_FD_WIFI_HT20_BGN
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    ESP_LOGI(TAG, "compat: 11b/g/n HT20");
#endif
#if CONFIG_FD_WIFI_TX_POWER_QDBM > 0
    esp_wifi_set_max_tx_power(CONFIG_FD_WIFI_TX_POWER_QDBM);
#endif   /* tat tiet kiem nang luong: on dinh hon khi auth/assoc */
    {
        int8_t p = 0; esp_wifi_get_max_tx_power(&p);
        wifi_country_t cc = {0}; esp_wifi_get_country(&cc);
        ESP_LOGI(TAG, "tx_power=%d (x0.25dBm) country=%.2s ch%u-%u", p, cc.cc, cc.schan, cc.schan + cc.nchan - 1);
    }

    saved_load();
    ESP_LOGI(TAG, "WiFi STA san sang, %d mang da luu", s_saved_n);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    connect_next_saved();
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

esp_err_t fd_net_scan_start(void)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_scanning) {
        xSemaphoreGive(s_mtx);
        return ESP_OK;
    }
    s_scanning = true;
    xEventGroupClearBits(s_eg, BIT_SCAN_DONE);
    esp_timer_stop(s_retry_timer);
    if (s_state == FD_NET_CONNECTING || s_state == FD_NET_WAIT_RETRY) {
        /* Dang thu ket noi thi khong quet duoc -> dung lai, quet xong se thu tiep */
        s_self_disconnect = (s_state == FD_NET_CONNECTING);
        esp_wifi_disconnect();
        s_state = FD_NET_IDLE;
    }
    xSemaphoreGive(s_mtx);
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Khong quet duoc: %s", esp_err_to_name(err));
        s_scanning = false;
    }
    return err;
}

int fd_net_scan_results(fd_net_ap_t *out, int max)
{
    if (!s_mtx) return 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int n = s_scan_n < max ? s_scan_n : max;
    memcpy(out, s_scan, n * sizeof(fd_net_ap_t));
    for (int i = 0; i < n; i++) out[i].saved = saved_find(out[i].ssid) >= 0;
    xSemaphoreGive(s_mtx);
    return n;
}

bool fd_net_scanning(void) { return s_scanning; }

/* Goi tu ngu canh nguoi goi (UI), KHONG giu s_mtx, khong chay trong event loop:
 * dang quet thi dung quet, cho SCAN_DONE (toi da 1 s) + POST_SCAN_MS roi moi ket noi. */
static void wait_scan_stopped(void)
{
    if (!s_scanning) return;
    ESP_LOGI(TAG, "Dang quet -> dung quet, cho SCAN_DONE");
    esp_wifi_scan_stop();
    EventBits_t b = xEventGroupWaitBits(s_eg, BIT_SCAN_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
    if (!(b & BIT_SCAN_DONE)) ESP_LOGW(TAG, "Het 1 s van chua SCAN_DONE");
    vTaskDelay(pdMS_TO_TICKS(POST_SCAN_MS));
}

esp_err_t fd_net_connect(const char *ssid, const char *pass)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    /* passphrase WPA2 8..63 ky tu (khong ho tro PSK hex 64 ky tu; cfg.sta.password chi 64 byte gom NUL) */
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (pass && strlen(pass) > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    wait_scan_stopped();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    esp_timer_stop(s_retry_timer);
    saved_net_t n = {0};
    strlcpy(n.ssid, ssid, sizeof(n.ssid));
    strlcpy(n.pass, pass ? pass : "", sizeof(n.pass));
    if (s_state == FD_NET_CONNECTED || s_state == FD_NET_CONNECTING) {
        s_self_disconnect = true;
        esp_wifi_disconnect();
    }
    s_target_new = true;
    s_new_attempts = 0;
    { int m = mode_slot(n.ssid, false); if (m >= 0 && !s_mode[m].ok) s_mode[m].compat = false; }
    do_connect(&n);
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

esp_err_t fd_net_connect_saved(const char *ssid)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    wait_scan_stopped();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int i = saved_find(ssid);
    if (i < 0) {
        xSemaphoreGive(s_mtx);
        return ESP_ERR_NOT_FOUND;
    }
    esp_timer_stop(s_retry_timer);
    saved_net_t n = s_saved[i];
    if (s_state == FD_NET_CONNECTED || s_state == FD_NET_CONNECTING) {
        s_self_disconnect = true;
        esp_wifi_disconnect();
    }
    s_target_new = false;
    s_backoff_n = 0;
    do_connect(&n);
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

esp_err_t fd_net_forget(const char *ssid)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int i = saved_find(ssid);
    if (i >= 0) {
        memmove(&s_saved[i], &s_saved[i + 1], (s_saved_n - i - 1) * sizeof(saved_net_t));
        s_saved_n--;
        saved_store();
        ESP_LOGD(TAG, "Da quen mang \"%s\"", ssid);
        if (strcmp(s_target.ssid, ssid) == 0 && s_state != FD_NET_IDLE) {
            esp_timer_stop(s_retry_timer);
            s_self_disconnect = (s_state == FD_NET_CONNECTED || s_state == FD_NET_CONNECTING);
            esp_wifi_disconnect();
            xEventGroupClearBits(s_eg, BIT_CONNECTED);
            s_state = FD_NET_IDLE;
            s_target.ssid[0] = 0;
            fd_evt_wifi_disc_t ev = {.reason = 0};
            strlcpy(ev.ssid, ssid, sizeof(ev.ssid));
            fd_events_post(FD_EVT_WIFI_DISCONNECTED, &ev, sizeof(ev));
            if (s_saved_n > 0) schedule_ms(RETRY_SAVED, RETRY_BASE_MS);
        }
    }
    xSemaphoreGive(s_mtx);
    return i >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool fd_net_is_saved(const char *ssid)
{
    if (!s_mtx) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool r = saved_find(ssid) >= 0;
    xSemaphoreGive(s_mtx);
    return r;
}

int fd_net_saved_count(void) { return s_saved_n; }
fd_net_state_t fd_net_state(void) { return s_state; }
bool fd_net_is_connected(void) { return s_eg && (xEventGroupGetBits(s_eg) & BIT_CONNECTED) != 0; }

bool fd_net_wait_connected(uint32_t timeout_ms)
{
    if (!s_eg) return false;
    return (xEventGroupWaitBits(s_eg, BIT_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms)) & BIT_CONNECTED) != 0;
}

int fd_net_rssi(void)
{
    wifi_ap_record_t ap;
    if (fd_net_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

void fd_net_ssid(char *out, size_t len)
{
    if (!s_mtx) { out[0] = 0; return; }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    strlcpy(out, s_target.ssid, len);
    xSemaphoreGive(s_mtx);
}

void fd_net_ip(char *out, size_t len) { strlcpy(out, s_ip, len); }

void fd_net_dbg_get(fd_net_dbg_t *out)
{
    out->disc_count = s_dbg_disc_n;
    out->last_reason = s_dbg_last_reason;
    out->last_reason_name = reason_name(s_dbg_last_reason);
    out->last_disc_us = s_dbg_last_us;
    out->backoff_n = s_backoff_n;
    out->next_retry_us = s_dbg_next_retry_us;
}
