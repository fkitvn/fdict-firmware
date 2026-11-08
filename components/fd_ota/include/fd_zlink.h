#pragma once
/* On-demand warranty lookup. Only the Warranty information screen requests it.
 * No boot/status-row fetch, activation-code creation or background polling. */
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { FD_WARR_IDLE, FD_WARR_LOADING, FD_WARR_READY, FD_WARR_ERROR } fd_warranty_state_t;
typedef struct {
    fd_warranty_state_t state;
    bool activated;
    bool active;
    char expires[16]; /* dd/mm/yyyy, UTC+7 */
    char err[24];
} fd_warranty_status_t;
void fd_zlink_refresh(void); /* async, duplicates coalesced; worker frees itself after response */
void fd_zlink_status_get(fd_warranty_status_t *out);
/* Independent, screen-scoped Zalo linking. All requests run off the LVGL task. */
typedef enum { FD_LINK_IDLE, FD_LINK_LOADING, FD_LINK_CODE, FD_LINK_DONE, FD_LINK_UNLINKING, FD_LINK_ERROR } fd_link_state_t;
typedef struct {
    fd_link_state_t state;
    char code[9];
    char err[24];
    int seconds;
} fd_link_status_t;
void fd_zalo_link_start(void);
void fd_zalo_link_stop(void);
void fd_zalo_link_unlink(void); /* Call only after explicit confirmation. */
void fd_zalo_link_tick(void);
void fd_zalo_link_get(fd_link_status_t *out);
#ifdef __cplusplus
}
#endif
