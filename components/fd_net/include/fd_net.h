#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FD_NET_MAX_SAVED 5
#define FD_NET_MAX_SCAN  20

typedef enum {
    FD_NET_IDLE = 0,        /* chua co mang luu / dang cho */
    FD_NET_CONNECTING,
    FD_NET_CONNECTED,
    FD_NET_WAIT_RETRY,      /* mat ket noi, cho backoff */
} fd_net_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
    bool saved;
} fd_net_ap_t;

/* Khoi tao netif + WiFi STA, nap danh sach mang da luu (NVS "wifi"), tu ket noi neu co. */
esp_err_t fd_net_init(void);

esp_err_t fd_net_scan_start(void);
int fd_net_scan_results(fd_net_ap_t *out, int max);
bool fd_net_scanning(void);

/* Ket noi mang moi (luu vao NVS khi co IP). pass = "" cho mang mo. */
esp_err_t fd_net_connect(const char *ssid, const char *pass);

/* Ket noi lai mang da luu (dung mat khau da luu). */
esp_err_t fd_net_connect_saved(const char *ssid);
esp_err_t fd_net_forget(const char *ssid);
bool fd_net_is_saved(const char *ssid);
int fd_net_saved_count(void);

fd_net_state_t fd_net_state(void);
bool fd_net_is_connected(void);
bool fd_net_wait_connected(uint32_t timeout_ms);
int fd_net_rssi(void);
void fd_net_ssid(char *out, size_t len);
void fd_net_ip(char *out, size_t len);

/* Chi doc (man Debug): bo dem mat ket noi tu luc khoi dong (khong tinh ngat do minh chu dong), backoff, lan thu lai ke tiep. */
typedef struct {
    uint32_t disc_count;
    int last_reason;
    const char *last_reason_name;
    int64_t last_disc_us;      /* esp_timer_get_time() luc mat ket noi cuoi */
    int backoff_n;
    int64_t next_retry_us;     /* hop le khi fd_net_state() == FD_NET_WAIT_RETRY */
} fd_net_dbg_t;
void fd_net_dbg_get(fd_net_dbg_t *out);


/* SNTP + mui gio (Kconfig FD_TZ, mac dinh ICT-7 = Asia/Bangkok) */
bool fd_net_time_valid(void);
/* "2026-10-17T08:05:00Z" (UTC) */
void fd_net_iso_utc(char *out, size_t len);

#ifdef __cplusplus
}
#endif
