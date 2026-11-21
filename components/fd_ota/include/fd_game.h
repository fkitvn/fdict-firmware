#pragma once
/* 4.8.0 Tro choi (Games): goi tro choi tai tu may chu.
 *  4.9.7: GIU MOI GOI DA TAI, moi game mot slot /data/games/<id>/ (game.json + meta.json, LittleFS phan vung storage);
 *  cap nhat chi thay slot cua game do. Danh sach may chu duoc cache o /data/games/list.json.
 *  - Danh sach: GET /api/devices/games?hw=fnk0104b&fw=x.y.z (Bearer thiet bi)
 *      -> {"games":[{"id","name","version","type","size","sha256","min_fw","path"}]}
 *    path la duong dan TUONG DOI tren cung may chu (bat dau "/api/"), khong nhan URL ngoai (token chi gui ve may chu FDict).
 *    404 = may chu chua co tinh nang nay -> err "none" (khong hien hop loi chung).
 *  - Goi: 1 file JSON <= FD_GAME_PKG_MAX, chi type "berry" (4.9.6 bo type catch):
 *      {"format":1,"id","name","version","type":"berry","min_fw","icon","config":{...},"script":"<Berry>"}
 *    Tai ve /data/game.dl -> kiem size + sha256 + min_fw + JSON hop le + id trung -> slot /data/games/<id>/.
 *    Chay trong may ao co gioi han (components/fd_berry).
 *  - 4.9.2: truong "icon" tuy chon (danh sach va goi): ten icon dung san trong firmware, chi [a-z0-9_-].
 *  - Thong so ngoai khoang hop le bi kep lai (khong tin du lieu tai ve). */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_GAME_ID_LEN    32
#define FD_GAME_NAME_LEN  48
#define FD_GAME_VER_LEN   16
#define FD_GAME_PATH_LEN  128
#define FD_GAME_LIST_MAX  12
#define FD_GAME_PKG_MAX   (128 * 1024)
#define FD_GAME_SCRIPT_MAX (64 * 1024)
#define FD_GAME_ICON_LEN  16   /* 4.9.2: ten icon dung san (paw, check, basket, joystick...), rong = theo type */

typedef struct {
    char id[FD_GAME_ID_LEN];
    char name[FD_GAME_NAME_LEN];
    char version[FD_GAME_VER_LEN];
    char min_fw[FD_GAME_VER_LEN];
    char sha256[65];
    char path[FD_GAME_PATH_LEN];
    int size;            /* byte, 0 = khong biet */
    bool supported;      /* type firmware nay chay duoc va firmware >= min_fw */
    bool berry;          /* 4.9.2: type berry */
    char icon[FD_GAME_ICON_LEN];
} fd_game_entry_t;

/* Mot goi da cai (meta + config cho fd.opt). */
typedef struct {
    char id[FD_GAME_ID_LEN];
    char name[FD_GAME_NAME_LEN];
    char version[FD_GAME_VER_LEN];
    char min_fw[FD_GAME_VER_LEN];
    bool runnable;   /* firmware dang chay >= min_fw */
    int fall_ms;     /* thoi gian the di het san o toc do Vua, the dau tien */
    int min_ms;      /* nhanh nhat */
    int every;       /* moi every cau dung thi len cap */
    int ramp_pct;    /* len cap: thoi gian *= ramp_pct / 100 */
    int lives;
    char icon[FD_GAME_ICON_LEN];   /* 4.9.2 */
} fd_game_cfg_t;

typedef enum {
    FD_GAME_IDLE = 0,
    FD_GAME_LISTING,
    FD_GAME_LIST_READY,
    FD_GAME_DOWNLOADING,
    FD_GAME_DONE,        /* cai xong */
    FD_GAME_ERROR,
} fd_game_state_t;

typedef struct {
    fd_game_state_t state;
    bool install;        /* loi / tien do thuoc lan cai (false = lan tai danh sach) */
    int pct;             /* 0..100 khi DOWNLOADING */
    int n;               /* so muc danh sach */
    char err[24];        /* no_wifi, unpaired, none, http_NNN, net, format, sha256, min_fw, too_large, fs, nomem */
} fd_game_status_t;

#define FD_GAME_INST_MAX 16
void fd_game_cfg_default(fd_game_cfg_t *c);
/* 4.9.7: cac goi da cai (doc meta tu LittleFS), sap theo ten. Tra so goi. */
int fd_game_installed_list(fd_game_cfg_t *out, int cap);
/* Doc goi da cai theo id. false = chua cai / hong. */
bool fd_game_load(const char *id, fd_game_cfg_t *out);
/* Ma nguon Berry cua goi id (PSRAM, nguoi goi free). NULL neu chua cai / hong. */
char *fd_game_script(const char *id, size_t *len);
/* Danh sach may chu lan tai thanh cong gan nhat (cache LittleFS), dung khi vua mo man hoac khong mang. */
int fd_game_list_cached(fd_game_entry_t *out, int cap);
/* Xoa moi goi da tai, slot cu va cache danh sach. */
bool fd_game_reset(void);   /* 5.0.0: true = da xoa het (kiem lai thu muc rong) */
/* 4.9.9: xoa slot /data/games/<id>/ (goi hong) */
void fd_game_remove(const char *id);
/* Tai danh sach (khong chan; ket qua qua fd_game_get / fd_game_list). */
void fd_game_list_fetch(void);
void fd_game_get(fd_game_status_t *out);
int fd_game_list(fd_game_entry_t *out, int cap);
/* Tai + kiem + cai 1 muc (khong chan). false neu dang ban hoac muc khong hop le. */
bool fd_game_install(const fd_game_entry_t *e);

#ifdef __cplusplus
}
#endif
