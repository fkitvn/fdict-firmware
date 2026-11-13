#pragma once
/* 2.1.6 Sap xep thanh cau tu API (cung mau fd_study / fd_flashcard):
 * POST {base}/api/study/sentence/start {device_id, count} -> {session_id, source, count,
 *   exercises:[{id, tokens[] (da xao), answer[], end, sentence, hint, word}]}
 * POST {base}/api/study/sentence/result {device_id, session_id, results:[{id, tokens[], ms}], best_streak, duration_ms}
 * Header Authorization: Bearer <device_token> (khong log). 401/410/429/message: fd_ota + ui_nav xu ly chung.
 * Mang chay trong task rieng "fd_sen" / "fd_sen_sync".
 * 2.1.7 OFFLINE: phien tai online luu /data/sen/cache.json (tmp+rename); khong mang -> dung cache (phien chua lam:
 * ket qua van gui duoc; da lam: xao lai, hoc vong lai) -> khong co cache: bo cau mau trong firmware.
 * Cham dap an tai may. Ket qua phien server -> hang doi /data/sen/o_<hex>.json, gui lai /result khi co mang
 * (200/409 used/400/403/404 = xoa; mang/401/429/5xx = giu). Sau khi gui het: nap san phien moi vao cache. */
#include <stdbool.h>
#include <stdint.h>
#include "fd_study.h"   /* fd_study_err_t */

#define FD_SEN_MAX      40
#define FD_SEN_TOK      9
#define FD_SEN_TOK_LEN  16
#define FD_SEN_ID_LEN   48
#define FD_SEN_TXT_LEN  160

typedef struct {
    char id[FD_SEN_ID_LEN];
    int n;                                   /* so the tu */
    char tok[FD_SEN_TOK][FD_SEN_TOK_LEN];    /* the da xao (thu tu hien thi) */
    char ans[FD_SEN_TOK][FD_SEN_TOK_LEN];    /* thu tu dung */
    char end[4];                             /* dau cau cuoi: . ? ! */
    char sentence[FD_SEN_TXT_LEN];
    char hint[FD_SEN_TXT_LEN];               /* nghia tieng Viet / goi y */
} fd_sen_ex_t;

typedef struct {
    bool busy;            /* dang tai / dang gui */
    fd_study_err_t err;   /* lan goi cuoi */
    int http;             /* ma HTTP cuoi (-1 loi mang, 0 chua goi) */
    int n;                /* so cau da nap */
    bool submitted;       /* da ghi ket qua (vao hang doi) */
    int source;           /* FD_SEN_SRC_* cua bo cau hien tai */
    int outbox;           /* so ket qua dang cho gui */
} fd_sen_status_t;

enum { FD_SEN_SRC_ONLINE = 0, FD_SEN_SRC_CACHE, FD_SEN_SRC_BUILTIN };

/* Gui hang doi ket qua + nap san cache khi co mang (khong chan, tu gioi han tan suat). */
void fd_sen_sync(void);
/* Tai bo cau moi (count 10/20, 0 = toi da FD_SEN_MAX). false = dang ban. */
bool fd_sen_start(int count);
void fd_sen_get(fd_sen_status_t *out);
/* Chep cau i vao out; false neu ngoai pham vi. */
bool fd_sen_ex(int i, fd_sen_ex_t *out);
/* Ghi cau tra loi cau i: order = chi so the (tok) theo thu tu nguoi dung chon. Tra true neu dung. */
bool fd_sen_answer(int i, const int *order, int n, uint32_t ms);
/* Ghi ket qua 1 lan vao hang doi (phien server) roi kick dong bo. false = da ghi / khong co phien. */
bool fd_sen_submit(int best_streak, uint32_t duration_ms);
/* Parse phan hoi /start (dung cho test). */
fd_study_err_t fd_sen_parse(const char *body, char *sid, int sid_cap, fd_sen_ex_t *out, int cap, int *n);
