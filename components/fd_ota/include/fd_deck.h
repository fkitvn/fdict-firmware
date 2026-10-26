#pragma once
/* Bo tu offline cho Hoc tap (hop dong: .cursor/docs/decks/CONTRACT-firmware.md).
 * Tai tu server khi online (task "fd_deck", uu tien thap): GET /api/v1/decks -> chon bo; words.tsv (ETag/304) +
 * audio.pak (Range noi tiep, If-Range). Luu /data/decks/<id>/ (thu muc .new roi rename = nguyen tu), 1 bo/luc,
 * quota FD_DECK_QUOTA. Hoc offline: tao phien tu bo da luu, cham diem tai may, ket qua xep /data/outbox
 * (gui POST /api/v1/study/offline-result khi online: 200/409 = xong, 401 = ve Chua ghep, giu outbox). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fd_study.h"

#define FD_DECK_QUOTA (4 * 1024 * 1024)

typedef enum { FD_DECK_IDLE = 0, FD_DECK_BUSY, FD_DECK_DONE, FD_DECK_ERROR } fd_deck_state_t;

typedef struct {
    fd_deck_state_t state;
    int pct;              /* 0..100 khi BUSY */
    char err[20];         /* no_wifi, unpaired, http_NNN, net, too_large, format, fs, nomem, no_deck */
    bool have;            /* co bo da luu day du */
    char id[24], name[40];
    int version, words;
    bool up_to_date;      /* lan tai cuoi: server bao khong doi */
    int outbox;           /* so ket qua cho gui */
} fd_deck_status_t;

void fd_deck_init(void);                 /* tao task + doc meta (goi nhieu lan duoc) */
void fd_deck_download(void);             /* xep lenh tai / cap nhat bo (khong chan) */
void fd_deck_get(fd_deck_status_t *out);
bool fd_deck_ready(void);
/* Lay 1 phien study/start da cache (du cau cua phien do, tron thu tu). fresh_only: chi phien dung chieu chua dung;
 * nguoc lai uu tien chua dung > da dung (offline: hoc vong lai). Goi tu task (doc file). */
fd_study_err_t fd_deck_build_session(fd_study_session_t *s, bool vi_en, bool fresh_only);
/* Ket qua phien offline (JSON) -> /data/outbox (tmp+rename); task fd_deck gui khi online. */
/* Luu phan hoi study/start vao /data/study_cache (<=16 phien, <=512 KB, tmp+rename). */
void fd_deck_cache_add(const char *direction, const char *json);
/* Phien da gui /api/study/result online (200/409): danh dau .usd. */
void fd_deck_cache_used(const char *session_id);
/* Tra phien da lay (.usd) ve chua dung (.json): phien nap truoc ma chua lam cau nao. */
void fd_deck_cache_unuse(const char *session_id);
/* Cache-first: co phien chua dung cho chieu nay trong /data/study_cache (chi liet ke thu muc, nhanh). */
bool fd_deck_has_fresh(bool vi_en);
/* Bao task fd_deck bu cache ngay (prefetch nen, khong chan). */
void fd_deck_refill(void);
void fd_deck_outbox_add(const char *session_id, const char *json);
/* Luu ket qua day du vao outbox (khong bao task gui ngay; dung truoc khi gui online de khong mat khi loi giua chung). */
bool fd_deck_outbox_save(const char *session_id, const char *json);
/* Luu tien do phien dang lam (<sid>.part, ghi sau moi cau); khoi dong lai -> thanh ket qua cho gui. */
bool fd_deck_outbox_progress(const char *session_id, const char *json);
/* Xoa ket qua + tien do cua phien (sau khi server nhan: 2xx/409, hoac tu choi vinh vien). */
void fd_deck_outbox_remove(const char *session_id);
/* Am thanh tu trong bo: true + duong dan audio.pak, offset, so byte PCM. Goi tu task am thanh (doc file). */
bool fd_deck_find_audio(const char *word, char *path, size_t plen, uint32_t *off, uint32_t *len);