#pragma once
/* 4.9.0 May ao Berry cho tro choi tai ve (goi type "berry", components/fd_ota/fd_game.c).
 *
 * Mo hinh: VM chay trong task rieng (stack PSRAM), script KHONG goi LVGL. Script ve bang API `fd` -> ghi vao bang
 * doi tuong dung chung (toi da FD_BERRY_OBJ_MAX hop chu); man hinh (scr_game.c, task LVGL) moi khung goi fd_berry_sync
 * de lay doi tuong thay doi va ve. Nut / cham -> fd_berry_event -> hang doi -> goi ham script.
 *
 * Script dinh nghia (deu tuy chon): init(), tick(dt_ms), button(n) n = Nut 1..4 nguoi dung thay, tap(id).
 * API (module toan cuc `fd`):
 *   fd.W, fd.H, fd.side ("bottom" | "top" | "left" | "right" = phia cum nut)
 *   fd.count() so tu; fd.en(i), fd.vi(i) tu thu i (0..count-1)
 *   fd.text(id, s) | fd.box(id, x, y, w, h) (w, h = 0: vua chu) | fd.color(id, nen, chu) (0xRRGGBB, nen < 0 = trong suot)
 *   fd.font(id, 14 | 18 | 32) | fd.show(id, bool) | fd.clear()
 *   fd.hud(diem, mang, cap) | fd.sound(dung) | fd.say(tu_tieng_anh) | fd.over(diem) | fd.rand(n) | fd.ms()
 *   print(...) ra log.
 * Gioi han: bo nho 512 KB (PSRAM), moi lan goi ham script toi da FD_BERRY_FRAME_MS (init FD_BERRY_INIT_MS), qua han
 * -> loi timeout, tro choi dung lai va bao loi. Khong file, mang, os, sys. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FD_BERRY_OBJ_MAX   16
#define FD_BERRY_TXT       96
#define FD_BERRY_MEM_CAP   (512 * 1024)
#define FD_BERRY_FRAME_MS  40
#define FD_BERRY_INIT_MS   1500
#define FD_BERRY_TICK_MS   33

typedef struct {
    const char *en;
    const char *vi;
} fd_berry_word_t;

typedef struct {
    char *script;               /* PSRAM, fd_berry nhan quyen so huu (free khi dung) */
    size_t len;
    const fd_berry_word_t *words;   /* chi doc; nguoi goi giu den khi fd_berry_stop xong */
    int nwords;
    int w, h;
    const char *side;
} fd_berry_cfg_t;

typedef struct {
    bool used, vis;
    int16_t x, y, w, h;
    int32_t bg, fg;             /* bg < 0 = trong suot */
    uint8_t font;               /* 14, 18, 32 */
    char txt[FD_BERRY_TXT];
} fd_berry_obj_t;

typedef enum { FD_BERRY_IDLE = 0, FD_BERRY_RUNNING, FD_BERRY_OVER, FD_BERRY_ERROR } fd_berry_state_t;

typedef struct {
    fd_berry_state_t state;
    bool hud_dirty;
    int score, lives, level;
    int sound;                  /* -1 khong, 0 sai, 1 dung */
    char say[48];
    int over_score;
    char err[96];
} fd_berry_misc_t;

typedef enum { FD_BERRY_EV_BUTTON = 1, FD_BERRY_EV_TAP } fd_berry_ev_t;

bool fd_berry_start(const fd_berry_cfg_t *cfg);
/* Dung VM (cho toi da ~500 ms). Goi duoc nhieu lan. */
void fd_berry_stop(void);
void fd_berry_event(fd_berry_ev_t type, int arg);
void fd_berry_pause(bool paused);
/* Lay doi tuong thay doi tu lan truoc (out[FD_BERRY_OBJ_MAX], tra mat na bit) va trang thai chung (xoa co sound / say / hud). */
uint32_t fd_berry_sync(fd_berry_obj_t *out, fd_berry_misc_t *misc);
size_t fd_berry_mem_used(void);

#ifdef __cplusplus
}
#endif
