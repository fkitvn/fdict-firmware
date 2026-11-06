#pragma once
/* 2.3.5: Tra tu online qua dict.fkit.vn (Bearer device_token) + lich su 30 tu + hang cho auto-add. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD_DICTNET_HIST_MAX 30
#define FD_DICTNET_WORD_LEN 48
#define FD_DICTNET_IPA_LEN  64
#define FD_DICTNET_MEAN_LEN 128
#define FD_DICTNET_POS_LEN  40

typedef struct {
    char en[FD_DICTNET_WORD_LEN];
    char vi[FD_DICTNET_MEAN_LEN];
    char ipa[FD_DICTNET_IPA_LEN];
    char pos[FD_DICTNET_POS_LEN];
} fd_dictnet_entry_t;

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
} fd_dictnet_err_t;

typedef struct {
    fd_dictnet_state_t state;
    fd_dictnet_err_t err;
    int http;
    fd_dictnet_entry_t result;
    bool added;          /* auto-add thanh cong trong lan lookup nay */
    bool queued;         /* add that bai -> da xep hang */
} fd_dictnet_status_t;

void fd_dictnet_init(void);
/* Tra cuu + (mac dinh) auto-add. false = dang ban. */
bool fd_dictnet_lookup(const char *word);
void fd_dictnet_get(fd_dictnet_status_t *out);
int fd_dictnet_history(fd_dictnet_entry_t *out, int cap);
/* Day hang cho add khi co Wi-Fi (goi tu task fd_dict / kick). */
void fd_dictnet_flush(void);