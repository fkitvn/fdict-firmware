#pragma once
/* 2.3.5: Tra tu online qua dict.fkit.vn (Bearer device_token) + lich su 30 tu + hang cho auto-add. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD_DICTNET_HIST_MAX 20   /* 4.1.7: 30 -> 20 (file cu 30 muc: hist_load cat con 20) */
#define FD_DICTNET_WORD_LEN 48
#define FD_DICTNET_IPA_LEN  64
#define FD_DICTNET_MEAN_LEN 128
#define FD_DICTNET_POS_LEN  40
/* 4.3.5: cau vi du + nghia ca cau (server lam day, khong luu lich su) */
#define FD_DICTNET_EX_LEN   320
#define FD_DICTNET_EXVI_LEN 512

typedef struct {
    char en[FD_DICTNET_WORD_LEN];
    char vi[FD_DICTNET_MEAN_LEN];
    char ipa[FD_DICTNET_IPA_LEN];
    char pos[FD_DICTNET_POS_LEN];
    uint8_t dir;         /* FD_DICTNET_DIR_*: chieu tra luc tao muc nay */
} fd_dictnet_entry_t;

#define FD_DICTNET_DIR_EN_VI 0
#define FD_DICTNET_DIR_VI_EN 1

typedef enum {
    FD_DICTNET_IDLE = 0,
    FD_DICTNET_LOOKUP,
    FD_DICTNET_ADD,
    FD_DICTNET_OK,
    FD_DICTNET_ERROR,
} fd_dictnet_state_t;

typedef enum {
    FD_DICTNET_ERR_NONE = 0,
    FD_DICTNET_ERR_NO_WIFI,
    FD_DICTNET_ERR_UNPAIRED,
    FD_DICTNET_ERR_NOT_FOUND,
    FD_DICTNET_ERR_NET,
    FD_DICTNET_ERR_SERVER,
    FD_DICTNET_ERR_FORMAT,
    FD_DICTNET_ERR_NOMEM,
    FD_DICTNET_ERR_STORAGE,
} fd_dictnet_err_t;

typedef struct {
    fd_dictnet_state_t state;
    fd_dictnet_err_t err;
    int http;
    fd_dictnet_entry_t result;
    bool added;          /* auto-add thanh cong trong lan lookup nay */
    bool queued;         /* add that bai -> da xep hang */
} fd_dictnet_status_t;

bool fd_dictnet_init(void);   /* false: clean failure, safe to retry */
/* Tra cuu + (mac dinh) auto-add. false = dang ban. */
bool fd_dictnet_lookup(const char *word);
/* dir = FD_DICTNET_DIR_*; VI_EN: word la tieng Viet, ket qua en = tu tieng Anh. */
bool fd_dictnet_lookup_dir(const char *word, uint8_t dir);
void fd_dictnet_get(fd_dictnet_status_t *out);
/* Cau vi du (tieng Anh) + nghia tieng Viet ca cau cua lan tra gan nhat (rong neu server khong co). */
void fd_dictnet_get_example(char *ex, size_t ex_cap, char *ex_vi, size_t vi_cap);
int fd_dictnet_history(fd_dictnet_entry_t *out, int cap);
/* Xoa lich su tra (RAM + /data/dict_hist.json/.tmp). Goi khi dang xuat / khoi phuc cai dat goc. */
void fd_dictnet_clear_history(void);
/* Day hang cho add khi co Wi-Fi (goi tu task fd_dict / kick). */
void fd_dictnet_flush(void);