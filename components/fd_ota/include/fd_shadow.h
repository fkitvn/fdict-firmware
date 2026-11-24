#pragma once
/* Nhai: POST /api/study/shadow/start. Luon can mang. Khong luu doan de hoc offline. */
#include <stdbool.h>
#include "fd_sync.h"

#define FD_SHADOW_LINES 8
#define FD_SHADOW_TXT   160
#define FD_SHADOW_VI    180
#define FD_SHADOW_IPA   120

typedef struct {
    char text[FD_SHADOW_TXT];
    char vi[FD_SHADOW_VI];
    char ipa[FD_SHADOW_IPA];
} fd_shadow_line_t;

enum { FD_SHADOW_IDLE = 0, FD_SHADOW_LOADING, FD_SHADOW_OK, FD_SHADOW_ERR };

typedef struct {
    int state;
    int http;
    bool empty;
    int n;
    char message[96];
    fd_shadow_line_t line[FD_SHADOW_LINES];
} fd_shadow_status_t;

/* length: "short" | "medium" | "long". ids rong = moi bo. false neu dang tai hoac khong tao duoc task. */
bool fd_shadow_start(const char *length, const char ids[][FD_SYNC_WID_LEN], int nids);
void fd_shadow_get(fd_shadow_status_t *out);
void fd_shadow_cancel(void);
