#pragma once
/* Buoc 2: Lien ket Zalo (sau Buoc 1 Dinh danh = ghep thiet bi).
 * Trang thai: POST {base}/api/devices/zalo-link/status (Bearer device_token) luc khoi dong + khi ve lai Cai dat.
 * Ma 6 so: POST /zalo-link/start -> hien ma (5 phut), poll /zalo-link/poll moi 4 s. Ma KHONG log, chi o RAM.
 * Moi viec mang chay trong task "fd_zlink" (khong chan UI). */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FD_ZL_UNKNOWN = -1, FD_ZL_NO = 0, FD_ZL_YES = 1 } fd_zlink_linked_t;
typedef enum { FD_ZLC_IDLE = 0, FD_ZLC_REQUESTING, FD_ZLC_CODE, FD_ZLC_DONE, FD_ZLC_EXPIRED, FD_ZLC_ERROR } fd_zlink_code_state_t;

typedef struct {
    fd_zlink_code_state_t state;
    char code[8];      /* 6 so khi CODE */
    int expires_s;     /* con lai */
    char err[20];
} fd_zlink_code_t;

void fd_zlink_refresh(void);               /* hoi trang thai (cho WiFi toi 90 s); bo qua neu chua ghep */
fd_zlink_linked_t fd_zlink_linked(void);   /* UNKNOWN khi chua ghep / chua hoi duoc */
uint32_t fd_zlink_gen(void);               /* tang moi khi trang thai doi (UI ve lai) */
void fd_zlink_code_start(void);            /* xin ma moi (huy ma cu) + poll */
void fd_zlink_code_stop(void);             /* dong hop ma: dung poll, xoa ma */
void fd_zlink_code_get(fd_zlink_code_t *out);

#ifdef __cplusplus
}
#endif
