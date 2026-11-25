#pragma once
/* Tinh nang tai ve (cung kieu tro choi): ma nguon nam tren may chu.
 * Danh sach nhan qua dong bo (/api/study/sync) va cache o /data/features/list.json.
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
bool fd_feature_has_list(void);
bool fd_feature_apply_sync(const char *buf, size_t len);
void fd_feature_mark_broken(const char *id);
bool fd_feature_is_broken(const char *id, const char *installed_ver);
void fd_feature_get(fd_feature_status_t *out);
bool fd_feature_find(const char *id, fd_feature_entry_t *out);
bool fd_feature_install(const fd_feature_entry_t *e);
/* 5.4.0: ma nguon Berry cua goi id (PSRAM, nguoi goi free). NULL neu chua cai / hong. */
char *fd_feature_script(const char *id, size_t *len);

#ifdef __cplusplus
}
#endif
