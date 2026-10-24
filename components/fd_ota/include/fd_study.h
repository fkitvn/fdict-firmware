#pragma once
/* Hoc tap tu API (hop dong: docs/study/CONTRACT-firmware.md).
 * POST {base}/api/study/start -> bo cau hoi; POST {base}/api/study/result (1 lan, du moi cau, bo qua = "").
 * Header Authorization: Bearer <device_token> (khong log). Moi viec mang chay trong task rieng "fd_study". */
#include <stdbool.h>
#include <stdint.h>

#define FD_STUDY_MAX      20
#define FD_STUDY_ID_LEN   48
#define FD_STUDY_TXT_LEN  128
#define FD_STUDY_IPA_LEN  64

typedef enum {
    FD_STUDY_IDLE = 0,
    FD_STUDY_LOADING,      /* dang goi /start */
    FD_STUDY_READY,        /* co bo cau hoi (fd_study_session) */
    FD_STUDY_SUBMITTING,   /* dang gui /result */
    FD_STUDY_DONE,         /* da gui xong (hoac 409 used = da gui truoc do) */
    FD_STUDY_ERROR,
} fd_study_state_t;

typedef enum {
    FD_STUDY_ERR_NONE = 0,
    FD_STUDY_ERR_NO_WIFI,
    FD_STUDY_ERR_UNPAIRED,  /* chua co token hoac 401 (token da bi xoa, ve Chua ghep) */
    FD_STUDY_ERR_NET,       /* loi mang / timeout */
    FD_STUDY_ERR_SERVER,    /* HTTP khac 200 */
    FD_STUDY_ERR_FORMAT,    /* JSON sai / thieu truong */
    FD_STUDY_ERR_NOMEM,
    FD_STUDY_ERR_NO_DECK,   /* hoc offline: chua tai bo tu */
} fd_study_err_t;

typedef struct {
    char id[FD_STUDY_ID_LEN];
    char prompt[FD_STUDY_TXT_LEN];
    char ipa[FD_STUDY_IPA_LEN];
    char choice[4][FD_STUDY_TXT_LEN];   /* theo key A, B, C, D */
    int8_t answer;                      /* 0..3 (A..D) */
} fd_study_q_t;

typedef struct {
    char session_id[FD_STUDY_ID_LEN];
    int n;
    bool offline;                       /* phien tu cache (fd_deck): cham tai may, ket qua vao outbox */
    fd_study_q_t q[FD_STUDY_MAX];
} fd_study_session_t;

typedef struct {
    fd_study_state_t state;   /* LOADING -> READY (co cau dau) | ERROR */
    fd_study_err_t err;
    int http;                 /* ma HTTP cuoi (-1 loi mang) */
} fd_study_status_t;

/* Bai hoc = chuoi cac phien server (moi phien ~8 cau, session_id rieng) noi lien nhau.
 * direction "en-vi"/"vi-en"; count 10/20 hoac 0 = Vo tan; source "deck" (cache chua dung -> study/start -> cache bat ky)
 * hoac "offline" (chi cache, hoc vong lai). Phien sau duoc nap nen truoc khi het cau. false = dang ban. */
bool fd_study_start(const char *direction, int count, const char *source);
void fd_study_get(fd_study_status_t *out);
/* Cau thu pos (0..) neu da nap; NULL = chua nap (dang tai) hoac het. */
const fd_study_q_t *fd_study_q(int pos);
/* So cau cua bai: count (10/20) - giam xuong so cau da nap neu khong con nguon; 0 = Vo tan. */
int fd_study_len(void);
/* true = khong con cau moi nao duoc nap (moi nguon deu loi/het) va dang khong tai. */
bool fd_study_exhausted(void);
/* Ghi cau tra loi (0..3, -1 bo qua) cho cau pos: luu tien do phien (.part); phien lam xong -> ket qua vao outbox. */
void fd_study_answer(int pos, int choice);
/* Ket thuc bai: complete = cac phien da lam do dang cung gui ket qua (cau chua lam = ""); !complete = giu tien do .part.
 * Phien da nap ma chua lam cau nao: tra ve cache. */
void fd_study_finish(bool complete);
/* Bo bai (vd roi man khi dang tai). */
void fd_study_cancel(void);
/* Parse phan hoi POST /api/study/start (choices [{key,text}], answer "A".."D"). Dung chung voi cache offline. */
fd_study_err_t fd_study_parse_start(const char *body, fd_study_session_t *s);
