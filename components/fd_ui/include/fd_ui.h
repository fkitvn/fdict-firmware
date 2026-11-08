#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FD_SCR_HOME = 0,
    FD_SCR_SETTINGS,
    FD_SCR_WIFI,
    FD_SCR_ABOUT,       /* Cai dat > Thong tin phien ban */
    FD_SCR_ROTATION,    /* Cai dat > Huong xoay man hinh */
    FD_SCR_BRIGHTNESS,  /* Cai dat > Do sang man hinh */
    FD_SCR_FLASHCARD,   /* Flash cards from synced decks */
    FD_SCR_STUDY,       /* Offline/cloud study and statistics */
    FD_SCR_SYNC,        /* Offline/cloud study and statistics */
    FD_SCR_STATS,       /* Offline/cloud study and statistics */
    FD_SCR_VOLUME,      /* Cai dat > Am luong */
    FD_SCR_TIMEOUT,     /* Cai dat > Thoi gian cho man hinh */
    FD_SCR_PIN,         /* Cai dat > Ma PIN; xac nhan tat may */
    FD_SCR_POS,         /* 2.3.5 Phan loai tu */
    FD_SCR_CLOZE,       /* 2.3.5 Dien tu */
    FD_SCR_DICT,        /* 2.3.5 Tu dien online */
    FD_SCR_PAIR,        /* Cai dat > Ghep thiet bi */
    FD_SCR_LANG,        /* Cai dat > Ngon ngu / Language (1.3.0) */
    FD_SCR_SENTENCE,    /* 2.1.6 Sap xep thanh cau */
    FD_SCR_MICTEST,     /* 2.1.8 Thu micro; 2.2.3 Che do nha phat trien > Thu loa & micro */
    FD_SCR_DEVMODE,     /* 2.2.2 Cai dat > Che do nha phat trien (cong tac + cong cu kiem tra) */
    FD_SCR_BTNTEST,     /* 2.2.2 ... > Thu 4 nut bam */
    FD_SCR_WARRANTY,    /* Cai dat > Thong tin bao hanh */
    FD_SCR_ZALO_LINK,
    FD_SCR_COUNT,
} fd_scr_t;

/* Dang ky xu ly su kien (nut, WiFi, gio) va hien man dau tien. Goi sau fd_display_init(). */
esp_err_t fd_ui_start(fd_scr_t first);
/* Chuyen man (an toan tu bat ky task/callback nao). */
void fd_ui_show(fd_scr_t scr);
/* Che do nha phat trien (Cai dat), luu NVS fd/devmode, mac dinh tat. */
bool fd_ui_devmode_get(void);
/* Thong bao ngan (vien bo tron gan day man, tu an ~1.2s, goi lai thi khoi dong lai, khong chong).
 * Goi trong task LVGL (dang giu lock). */
void fd_ui_toast(const char *text);

#ifdef __cplusplus
}
#endif
