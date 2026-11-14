#pragma once
/* 2.3.0 Dong bo (module "Dong bo"): web app / cloud la NGUON DUNG cho cai dat + bo tu.
 * Khi co Wi-Fi + da ghep (task "fd_sync", tu chay nen):
 *  4.1.3: MOI lenh di qua 1 API POST /api/study/sync {device_id, events?, rev?, offset?, limit?}
 *    -> {acked, rev/etag, settings, unchanged | words[] (co the rong), next_offset}.
 *  - Kiem tra re: (sync, rev + limit 1) {device_id} -> {rev}; rev khac rev da luu (hoac Dong bo ngay) ->
 *    (sync, trang) {device_id, offset, limit} theo trang -> ghi /data/sync/store.tmp -> fsync -> rename
 *    /data/sync/store.tsv (nguyen tu; thay TOAN BO ban cu => tu/bo/cai dat bi xoa tren cloud cung bi xoa tren may).
 *  - Ket qua hoc offline: moi cau tra loi = 1 dong JSON {eid,k,wid,ok,ts} noi vao /data/sync/events.log (fsync);
 *    cau AI them m (order|cloze|hear) va sen (loi cau). Diem cau nam tren server, khong ghi vao kho may.
 *    gui (sync, events) (lo <= 50, eid chong trung), chi xoa dong khi server ack.
 *  - 401: fd_ota xoa token (ve Chua ghep), GIU du lieu + outbox; 429: fd_ota cho Retry-After, sau do backoff.
 * Hoc offline 100%: Luyen tap / Flash card / Sap xep cau doc tu kho nay (khong mang).
 * Dinh dang store.tsv (UTF-8, tab, 1 ban ghi/dong):
 *   #FDS1<TAB>rev<TAB>epoch_tai
 *   S<TAB>uu_tien_tu_moi(0/1)<TAB>uu_tien_chua_thuoc(0/1)<TAB>chu_de,chu_de
 *   C<TAB>id<TAB>mac_dinh(0/1)<TAB>ten
 *   W<TAB>id<TAB>collection_id<TAB>status(0 new,1 learning,2 mastered)<TAB>created_epoch<TAB>en<TAB>ipa<TAB>vi<TAB>pos<TAB>(ex de trong)<TAB>n<TAB>(en<TAB>vi) x n
 *   #END<TAB>so_tu<TAB>so_bo<TAB>so_cau   (thieu/sai = file hong -> bo, tai lai toan bo) */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "fd_study.h"
#include "fd_flashcard.h"
#include "fd_sentence.h"

#define FD_SYNC_MAX_WORDS 3000
#define FD_SYNC_WID_LEN   25

typedef enum { FD_SYNC_IDLE = 0, FD_SYNC_CHECKING, FD_SYNC_PULLING, FD_SYNC_PUSHING, FD_SYNC_OK, FD_SYNC_ERROR } fd_sync_state_t;

typedef struct {
    fd_sync_state_t state;
    int pct;                 /* 0..100 khi PULLING */
    char err[24];            /* lan loi cuoi: no_wifi, unpaired, no_account, http_NNN, net, format, fs, nomem, too_large */
    bool have;               /* co kho tu hop le */
    int words, sentences, collections;
    int pending;             /* ket qua hoc chua gui */
    time_t last_sync;        /* lan doi chieu thanh cong cuoi voi server (0 = chua / chua co gio) */
    bool prio_new, prio_unl; /* cai dat tu web app */
    int topics;              /* so chu de quan tam */
    bool manual;             /* dang chay "Dong bo ngay" */
    uint32_t force_done;     /* 4.1.4: so lan "Dong bo ngay" da chay xong (UI cho ket qua) */
    bool force_ok;           /* ket qua lan "Dong bo ngay" cuoi */
} fd_sync_status_t;

typedef enum { FD_STATS_IDLE = 0, FD_STATS_LOADING, FD_STATS_OK, FD_STATS_ERROR } fd_stats_state_t;

typedef struct {
    fd_stats_state_t state;
    char err[24];            /* no_wifi, unpaired, no_account, http_NNN, net, format */
    int words, n_new, learning, mastered, collections, sentences;
    int sen_mastered, sen_learning, sen_order, sen_cloze, sen_hear;
    int today_answers, today_correct, week_answers, week_correct, week_sessions, streak_days;
    int scan_uses, scan_words;   /* luot scan AI / tu luu tu scan (server) */
} fd_sync_stats_t;

void fd_sync_init(void);                 /* goi 1 lan sau khi mount /data (nap kho + outbox, tao task) */
void fd_sync_kick(void);                 /* co ket qua moi: gui outbox som */
void fd_sync_force(void);                /* nut "Dong bo ngay": gui outbox + tai lai toan bo */
void fd_sync_get(fd_sync_status_t *out);
bool fd_sync_ready(void);                /* kho co >= 4 tu (du tao trac nghiem) */
/* Tao phien Luyen tap (Practice) tu kho (theo uu tien hoc). n <= FD_STUDY_MAX. session_id "lq-..." (ket qua = su kien fd_sync_event). */
fd_study_err_t fd_sync_build_practice(fd_study_session_t *s, bool vi_en, int n);
/* Trac nghiem phan loai tu (POS) / dien tu cho trong tu kho dong bo (4 dap an). */
fd_study_err_t fd_sync_build_pos_quiz(fd_study_session_t *s, int n);
fd_study_err_t fd_sync_build_cloze_quiz(fd_study_session_t *s, int n);
/* Returns count, 0 when no data, -1 on PSRAM exhaustion. */
int fd_sync_build_cards(fd_fc_card_t *out, int cap);
/* Ghi 1 lan tu duoc hien tren Lat tu. Dem nam trong kho RAM va /data/sync/card_seen.log (con sau khi dong bo lai). */
void fd_sync_card_seen(const char *wid);
/* Bai sap xep cau tu cau vi du trong kho: dien ans/n/end/sentence/hint (tok = ans, nguoi goi tu xao); wid = id tu. */
int fd_sync_build_sentences(fd_sen_ex_t *out, char (*wid)[FD_SYNC_WID_LEN], int cap);
/* Ghi 1 ket qua hoc (kind "quiz" | "card" | "sen") vao outbox ben vung. */
void fd_sync_event(const char *kind, const char *wid, bool correct);
/* Cau AI: mod "order" | "cloze" | "hear". Server chi danh dau da thuoc khi du 3 module. */
void fd_sync_event_sen(const char *wid, const char *sentence, bool correct, const char *mod);
/* Thong ke: LUON goi server (gui outbox truoc). Khong Wi-Fi -> ERROR "no_wifi". */
void fd_sync_stats_fetch(void);
void fd_sync_stats_get(fd_sync_stats_t *out);

/* 4.3.2: cai dat chon bo tu DUNG CHUNG 5 module hoc (Lat tu, Luyen tap, Sap xep cau, Phan loai tu, Dien tu). */
#define FD_SYNC_MAX_COLS      32
#define FD_SYNC_COL_NAME_LEN  48
#define FD_SYNC_PICK_F_SEN    1u  /* tu trong bo da chon, va tu do co cau AI (Sap xep cau, Dien tu) */
#define FD_SYNC_PICK_F_TEXT   2u  /* can en+vi (Lat tu) */

typedef enum {
    FD_SYNC_PICK_ALL = 0,       /* Tat ca */
    FD_SYNC_PICK_NEW = 1,       /* Tu moi them (status = 0) */
    FD_SYNC_PICK_COMBINED = 2,  /* Ket hop: cac bo tu da chon */
} fd_sync_pick_mode_t;

typedef struct {
    char id[FD_SYNC_WID_LEN];
    char name[FD_SYNC_COL_NAME_LEN];
    bool def;
} fd_sync_col_t;

typedef struct {
    fd_sync_pick_mode_t mode;
    int nsel;                    /* so bo dang chon khi Ket hop */
} fd_sync_pick_t;

int  fd_sync_col_count(void);
bool fd_sync_col_get(int i, fd_sync_col_t *out);
void fd_sync_pick_get(fd_sync_pick_t *out);
void fd_sync_pick_set_mode(fd_sync_pick_mode_t mode);
void fd_sync_pick_toggle_col(int col_idx);
void fd_sync_pick_set(fd_sync_pick_mode_t mode, const char ids[][FD_SYNC_WID_LEN], int n);

bool fd_sync_pick_col_on(int col_idx);
/* Nhat tu theo cai dat bo tu chung. flags: FD_SYNC_PICK_F_SEN / F_TEXT. */
int  fd_sync_pick_words(int *idx, int cap, unsigned flags);
int  fd_sync_pick_count(unsigned flags);
