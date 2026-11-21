#pragma once
/* 4.8.0 Tro choi (Games): goi tro choi tai tu may chu, CHI 1 GOI CAI tai mot thoi diem (tiet kiem bo nho).
 *  - Danh sach: GET /api/devices/games?hw=fnk0104b&fw=x.y.z (Bearer thiet bi)
 *      -> {"games":[{"id","name","version","type","size","sha256","min_fw","path"}]}
 *    path la duong dan TUONG DOI tren cung may chu (bat dau "/api/"), khong nhan URL ngoai (token chi gui ve may chu FDict).
 *    404 = may chu chua co tinh nang nay -> err "none" (khong hien hop loi chung).
 *  - Goi (giai doan 1, chi type "catch" = tro choi Hung tu dung san trong firmware): 1 file JSON <= FD_GAME_PKG_MAX
 *      {"format":1,"id","name","version","type":"catch","min_fw":"4.8.0",
 *       "config":{"fall_ms","min_ms","every","ramp_pct","lives"}, "words":[{"en","vi"}] (tuy chon)}
 *    Tai ve /data/game.dl -> kiem size + sha256 + min_fw + JSON hop le + id trung -> thay /data/game/game.json.
 *  - 4.9.0: type "berry" = tro choi viet bang Berry: them "script" (ma nguon Berry, <= FD_GAME_SCRIPT_MAX), "words" tuy chon.
 *    Chay trong may ao co gioi han (components/fd_berry). Goi berry phai co min_fw >= 4.9.0 (may chu kiem khi tai len).
 *  - 4.9.2: truong "icon" tuy chon (danh sach va goi): ten icon dung san trong firmware, chi [a-z0-9_-].
 *  - Thong so ngoai khoang hop le bi kep lai (khong tin du lieu tai ve). */
#include <stdbool.h>
#include <stddef.h>
#include "fd_sync.h"   /* fd_spell_word_t (en, vi) */

#ifdef __cplusplus
extern "C" {
#endif

#define FD_GAME_ID_LEN    32
#define FD_GAME_NAME_LEN  48
#define FD_GAME_VER_LEN   16
#define FD_GAME_PATH_LEN  128
#define FD_GAME_LIST_MAX  12
#define FD_GAME_PKG_MAX   (128 * 1024)
#define FD_GAME_WORDS_MAX 500
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
    bool installed;      /* trung id + version voi goi dang cai */
    bool berry;          /* 4.9.2: type berry */
    char icon[FD_GAME_ICON_LEN];
} fd_game_entry_t;

/* Thong so tro choi Hung tu (mac dinh trong firmware hoac tu goi da cai). */
typedef struct {
    char id[FD_GAME_ID_LEN];        /* rong = tro choi dung san */
    char name[FD_GAME_NAME_LEN];
    char version[FD_GAME_VER_LEN];
    int fall_ms;     /* thoi gian the di het san o toc do Vua, the dau tien */
    int min_ms;      /* nhanh nhat */
    int every;       /* moi every cau dung thi len cap */
    int ramp_pct;    /* len cap: thoi gian *= ramp_pct / 100 */
    int lives;
    int nwords;      /* so tu kem trong goi (0 = dung kho tu cua may) */
    bool berry;      /* 4.9.0: goi type berry (co script) */
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

void fd_game_cfg_default(fd_game_cfg_t *c);
/* Doc goi dang cai (/data/game/game.json). false = chua cai hoac goi hong (khi do *out = mac dinh). */
bool fd_game_installed(fd_game_cfg_t *out);
/* Tu kem trong goi dang cai (en + vi), da xao. Tra so tu. */
int fd_game_pkg_words(fd_spell_word_t *out, int cap);
/* 4.9.0: ma nguon Berry cua goi dang cai (PSRAM, nguoi goi free). NULL neu khong phai goi berry. */
char *fd_game_pkg_script(size_t *len);
/* Tai danh sach (khong chan; ket qua qua fd_game_get / fd_game_list). */
void fd_game_list_fetch(void);
void fd_game_get(fd_game_status_t *out);
int fd_game_list(fd_game_entry_t *out, int cap);
/* Tai + kiem + cai 1 muc (khong chan). false neu dang ban hoac muc khong hop le. */
bool fd_game_install(const fd_game_entry_t *e);

#ifdef __cplusplus
}
#endif
