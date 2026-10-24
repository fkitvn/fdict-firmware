/*
 * fd_t9kb - Ban phim T9 (multi-tap) cho man hinh nho 320x240, LVGL 9.6.
 * Dung de nhap mat khau WiFi (thay lv_keyboard phim qua nho).
 *
 * Chi duoc goi tu LVGL task / khi dang giu LVGL lock.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FD_T9KB_MODE_LOWER = 0,  /* abc  */
    FD_T9KB_MODE_UPPER,      /* ABC  */
    FD_T9KB_MODE_DIGIT,      /* 123  */
    FD_T9KB_MODE_SYMBOL,     /* #+=  */
    FD_T9KB_MODE_COUNT
} fd_t9kb_mode_t;

typedef enum {
    FD_T9KB_LAYOUT_T9 = 0,   /* ban phim T9 phim lon (mac dinh) */
    FD_T9KB_LAYOUT_QWERTY,   /* lv_keyboard QWERTY, phim "T9" de quay lai */
} fd_t9kb_layout_t;

/** Goi khi bam phim Xong (✓). text = noi dung textarea (da commit ky tu dang cho). */
typedef void (*fd_t9kb_done_cb_t)(lv_obj_t *kb, const char *text, void *user_data);

/**
 * Tao ban phim T9 (lv_obj container) trong parent.
 * Kich thuoc mac dinh: rong 100% parent, cao FD_T9KB_DEF_HEIGHT (146 px).
 * Co the doi bang lv_obj_set_size / flex_grow; phim tu dan theo grid.
 * textarea: textarea dich (co the NULL roi gan sau bang fd_t9kb_set_textarea).
 * Moi tai nguyen (timer, ctx) duoc giai phong khi kb bi xoa (LV_EVENT_DELETE).
 */
lv_obj_t *fd_t9kb_create(lv_obj_t *parent, lv_obj_t *textarea,
                         fd_t9kb_done_cb_t on_done, void *user_data);

void            fd_t9kb_set_textarea(lv_obj_t *kb, lv_obj_t *textarea);
void            fd_t9kb_set_mode(lv_obj_t *kb, fd_t9kb_mode_t mode);
fd_t9kb_mode_t  fd_t9kb_get_mode(lv_obj_t *kb);
/** Phim mode: abc -> ABC -> #+= -> abc (khong gom 123). Tu 123: chuyen sang che do ke tiep cua che do truoc. */
void            fd_t9kb_next_mode(lv_obj_t *kb);
/** Phim 123/abc: vao che do so (moi phim = 1 chu so) / quay lai che do chu truoc do. */
void            fd_t9kb_toggle_digits(lv_obj_t *kb);
/** Thoi gian cho commit (ms), mac dinh 900. */
void            fd_t9kb_set_timeout(lv_obj_t *kb, uint32_t ms);

/** Chuyen T9 <-> QWERTY (commit ky tu dang cho; QWERTY tao lan dau, sau do chi an/hien; giai phong cung kb). */
void             fd_t9kb_set_layout(lv_obj_t *kb, fd_t9kb_layout_t layout);
fd_t9kb_layout_t fd_t9kb_get_layout(lv_obj_t *kb);

/* Cac thao tac cho phim cung (B1..B4) - tuy chon. */
void fd_t9kb_commit(lv_obj_t *kb);     /* chot ky tu dang cho */
void fd_t9kb_backspace(lv_obj_t *kb);
void fd_t9kb_clear(lv_obj_t *kb);
void fd_t9kb_done(lv_obj_t *kb);       /* commit + goi on_done */

#define FD_T9KB_DEF_HEIGHT 146

#ifdef __cplusplus
}
#endif
