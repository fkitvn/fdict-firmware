#pragma once
/* OTA tu API (hop dong: docs/ota/CONTRACT-firmware.md muc 3.4, 10).
 * Kiem tra: GET {FD_OTA_BASE_URL}/ota/check; tai: esp_https_ota (Range ~64 KB, noi lai khi WiFi yeu);
 * xac minh SHA256 toan bo .bin + esp_app_desc; bao cao: POST /ota/report (hang doi NVS, best-effort).
 * Moi viec mang chay trong task "fd_ota" (khong chan UI). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FD_OTA_ST_IDLE = 0,       /* chua kiem tra */
    FD_OTA_ST_CHECKING,       /* Dang kiem tra... */
    FD_OTA_ST_UP_TO_DATE,     /* Da la ban moi nhat */
    FD_OTA_ST_AVAILABLE,      /* Co ban moi X (size) [Cap nhat] */
    FD_OTA_ST_DOWNLOADING,    /* Dang tai N% */
    FD_OTA_ST_ERROR,          /* Loi: <err> */
    FD_OTA_ST_REBOOT_PENDING, /* Se khoi dong lai khi ranh */
} fd_ota_state_t;

typedef struct {
    fd_ota_state_t state;
    char version[24];         /* fw_version ban moi */
    char build[20];           /* build_id ban moi */
    uint32_t size;            /* byte */
    bool mandatory;
    int pct;                  /* 0..100 khi DOWNLOADING */
    char notes[160];          /* notes_vi */
    char err[20];             /* ma ngan: no_wifi, no_time, batt_low, too_large, net, http_NNN, verify_failed,
                                 bad_image, flash, busy */
} fd_ota_status_t;

/* Goi 1 lan sau khi display + storage da khoi tao (app_main). Neu firmware dang PENDING_VERIFY: tu kiem tra
 * (display_ok, storage_ok, WiFi ket noi trong 60 s) -> hop le + bao booted_ok, hoac rollback + khoi dong lai.
 * Khoi dong task OTA (kiem tra sau WiFi + 30..120 s ngau nhien, roi dinh ky; khong cho dong bo gio SNTP). */
esp_err_t fd_ota_start(bool display_ok, bool storage_ok);

/* UI cung cap: thiet bi ranh (khong thao tac >= 60 s hoac man hinh tat) va hien thong bao truoc khi khoi dong lai
 * (goi tu task fd_ota, khong giu khoa LVGL). */
void fd_ota_set_hooks(bool (*is_idle)(void), void (*before_reboot)(void));

void fd_ota_get_status(fd_ota_status_t *out);
void fd_ota_check_now(void);    /* nut "Kiem tra cap nhat" */
void fd_ota_reboot_now(void);   /* nut "Khoi dong lai ngay": chi khi REBOOT_PENDING, cung duong khoi dong lai khi ranh */
void fd_ota_update_now(void);   /* nut [Cap nhat]: tai ngay (khong cho ranh); khoi dong lai van cho ranh */

/* Kenh OTA: luon "stable". */
void fd_ota_get_channel(char *out, size_t len);
/* Base dang dung (khong co /api); *lab = true neu dang dung server Lab (che do nha phat trien). */
void fd_ota_get_base(char *out, size_t len, bool *lab);
/* Server chon luc chay (NVS fd/devmode doc luc fd_ota_start): Lab = CONFIG_FD_OTA_LAB_BASE_URL / CONFIG_FD_PAIR_LAB_WEB_URL,
 * chinh thuc = CONFIG_FD_OTA_BASE_URL / CONFIG_FD_PAIR_WEB_URL (https + cert bundle). Moi request doc lai -> doi ngay. */
const char *fd_ota_base_url(void);
const char *fd_ota_pair_web_url(void);
bool fd_ota_is_lab(void);
uint32_t fd_ota_server_gen(void);
/* 23/10: thong bao loi gan nhat tu may chu (truong "message" cua body JSON khi HTTP >= 400, hoac cau mac dinh).
 * Tra so lan (tang moi thong bao moi); code = HTTP status. UI hien khi so lan doi. */
uint32_t fd_ota_server_msg(char *out, size_t n, int *code);   /* tang moi lan doi server (UI ve lai) */
/* Doi server (thread-safe, goi tu UI khi bat/tat che do nha phat trien): URL moi ap dung ngay; task fd_ota huy phien ghep,
 * huy tai OTA dang chay, xoa NVS fdict/device_token + user_name, bo hang doi bao cao, kiem tra OTA lai sau 5 s. */
void fd_ota_set_server(bool lab);

/* Ghep thiet bi: POST {base}/api/devices/pair/start -> mo phien 10 phut (bo qua pair_code), poll /pair/poll moi 5 s (410 -> tu mo phien moi);
 * paired -> luu NVS fdict/device_token (khong log/hien thi). Dung khi roi man hinh (fd_ota_pair_stop). */
typedef enum { FD_PAIR_IDLE = 0, FD_PAIR_REQUESTING, FD_PAIR_CODE, FD_PAIR_DONE, FD_PAIR_ERROR } fd_pair_state_t;
typedef struct {
    fd_pair_state_t state;
    uint32_t session;    /* tang moi lan pair/start thanh cong (UI ve lai QR) */
    int expires_s;       /* con lai */
    bool paired;         /* da co token */
    char err[20];
} fd_pair_status_t;
void fd_ota_pair_start(void);
void fd_ota_pair_stop(void);
/* Huy ghep: dung ghep + xoa NVS fdict/device_token (xu ly trong task fd_ota, theo thu tu lenh). */
void fd_ota_unpair(void);
/* Ten tai khoan da ghep (tu phan hoi pair/poll, luu NVS fdict/user_name). false = chua co. */
bool fd_ota_get_user_name(char *out, size_t len);
void fd_ota_pair_get(fd_pair_status_t *out);
bool fd_ota_is_paired(void);

/* Truoc khi ngu (deep sleep = khoi dong lai -> bootloader coi PENDING_VERIFY la loi): cho tu kiem tra xong
 * (toi da timeout_ms). */
void fd_ota_selftest_wait(uint32_t timeout_ms);

/* API chung cho fd_study: POST {base}{path} JSON + Bearer device_token (khong log); 401 -> xoa token, ve Chua ghep.
 * Goi tu task rieng (khong phai LVGL). Tra HTTP status, -1 = loi mang. resp = body (cat theo cap, ket thuc 0). */
int fd_ota_api_post(const char *path, const char *json, char *resp, int cap);
/* Nhu tren; retry_429 = false: khong cho Retry-After (dung cho thao tac nguoi dung dang doi). */
int fd_ota_api_post_ex(const char *path, const char *json, char *resp, int cap, bool retry_429);
/* GET {base}{path} + Bearer -> ghi body vao file dst (chi khi 200, <= max_bytes; loi -> xoa dst). Task rieng, khong LVGL.
 * Tra HTTP status; -1 mang/URL, -2 qua lon, -3 loi file. 401 -> xoa token, ve Chua ghep. */
int fd_ota_api_get_file(const char *path, const char *dst, int max_bytes);
/* Nhu tren + ETag/Range (noi tiep kieu OTA): resume -> Range tu kich thuoc dst (+If-Range etag_in); khong resume +
 * etag_in -> If-None-Match (304). etag_out = ETag phan hoi. prog(got,total,ctx) goi tu task goi. */
int fd_ota_api_get_file_ex(const char *path, const char *dst, int max_bytes, bool resume, const char *etag_in,
                           char *etag_out, size_t etag_cap, void (*prog)(int got, int total, void *ctx), void *ctx);
/* device_id = MAC goc eFuse, 12 hex thuong */
void fd_ota_get_device_id(char *out, size_t len);

#ifdef __cplusplus
}
#endif
