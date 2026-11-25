#pragma once
/* Tinh nang tai ve (cung kieu tro choi): ma nguon nam tren may chu.
 * GET /api/devices/features?hw=&fw=&device_id= -> { features: [{ id, version, sha256, path, ... }] }
 * Goi JSON co truong script. Slot /data/features/<id>/feature.json + meta.json. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_FEAT_ID_LEN  32
#define FD_FEAT_VER_LEN 16
#define FD_FEAT_PATH_LEN 160
#define FD_FEAT_LIST_MAX 8
#define FD_FEAT_PKG_MAX (64 * 1024)
#define FD_FEAT_SCRIPT_MAX (32 * 1024)   /* 5.4.0, khop server */

typedef struct {
    char id[FD_FEAT_ID_LEN];
    char version[FD_FEAT_VER_LEN];
    char min_fw[FD_FEAT_VER_LEN];
    char sha256[65];
    char path[FD_FEAT_PATH_LEN];
    int size;
    bool supported;
} fd_feature_entry_t;

typedef enum {
    FD_FEAT_IDLE = 0,
    FD_FEAT_LISTING,
    FD_FEAT_LIST_READY,
    FD_FEAT_DOWNLOADING,
    FD_FEAT_DONE,
    FD_FEAT_ERROR,
} fd_feature_state_t;

typedef struct {
    fd_feature_state_t state;
    bool install;
    int pct;
    char err[24];
    uint32_t list_seq;
} fd_feature_status_t;

bool fd_feature_local(const char *id, char *version, size_t n);
void fd_feature_load_cache(void);
bool fd_feature_list_stale(uint32_t ttl_ms);
void fd_feature_list_invalidate(void);
bool fd_feature_should_poll(uint32_t ttl_ms);
void fd_feature_list_fetch(void);
void fd_feature_get(fd_feature_status_t *out);
bool fd_feature_find(const char *id, fd_feature_entry_t *out);
bool fd_feature_install(const fd_feature_entry_t *e);
/* 5.4.0: ma nguon Berry cua goi id (PSRAM, nguoi goi free). NULL neu chua cai / hong. */
char *fd_feature_script(const char *id, size_t *len);

#ifdef __cplusplus
}
#endif
