#pragma once
/* Flash card tu API (chi DOC), cung mau voi fd_study (.cursor/docs/study/CONTRACT-firmware.md):
 * POST {base}/api/study/cards {device_id, offset, limit} + Authorization: Bearer <device_token> (khong log)
 * -> {source:"deck"|"mockup", total, offset, cards:[{id, word, ipa, meaning}]}.
 * Mang chay trong task rieng "fd_fc"; phan hoi 200 luu /data/fc_cache.json (tmp+rename) -> offline/chua ghep dung cache;
 * khong co cache -> UI dung bo tu mau co san. 401: fd_ota xoa token (ve Chua ghep); 429: fd_ota cho Retry-After. */
#include <stdbool.h>
#include "fd_study.h"   /* fd_study_err_t */

#define FD_FC_MAX      100
#define FD_FC_WORD_LEN 48
#define FD_FC_IPA_LEN  64
#define FD_FC_MEAN_LEN 128

typedef struct {
    char word[FD_FC_WORD_LEN];
    char ipa[FD_FC_IPA_LEN];
    char meaning[FD_FC_MEAN_LEN];
} fd_fc_card_t;

typedef struct {
    bool busy;            /* dang tai */
    fd_study_err_t err;   /* lan tai cuoi */
    int http;             /* ma HTTP cuoi (-1 loi mang, 0 chua goi) */
    int n;                /* so the dang co (server hoac cache); 0 = dung bo mau */
    bool online;          /* the lay tu server trong lan chay nay (khong phai cache) */
} fd_fc_status_t;

/* Nap cache (lan dau) + xep tai the tu server neu da ghep & co WiFi (khong chan). false = dang ban. */
bool fd_fc_refresh(void);
void fd_fc_get(fd_fc_status_t *out);
/* Chep toi da cap the hien co vao out (an toan luong). Tra so the da chep. */
int fd_fc_copy(fd_fc_card_t *out, int cap);
/* Parse phan hoi POST /api/study/cards. Dung chung voi cache. */
fd_study_err_t fd_fc_parse(const char *body, fd_fc_card_t *out, int cap, int *n);
