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
 *   fd.hud(diem, mang, cap[, combo]) | fd.sound(dung) | fd.say(tu_tieng_anh) | fd.over(diem[, so_dung, cap]) | fd.rand(n) | fd.ms()
 *   4.9.5 (moi tro choi, ke ca Hung tu dung san, la script Berry; hoat hinh va ve van la C):
 *   fd.style(id, vien_px, mau_vien, bo_goc, dem) | fd.measure(chu, font, rong_toi_da) -> [w, h] cua chu (px)
 *   fd.move(id, x, y, ms): C dich doi tuong tu vi tri hien tai toi (x, y) trong ms (tuyen tinh, moi khung man hinh),
 *     toi noi thi goi arrive(id) trong script | fd.stop(id): dung tai cho
 *   fd.opt(ten): "dir" "spd" "rep" (cai dat nguoi choi), "fall_ms" "min_ms" "every" "ramp_pct" "lives" (config goi)
 *   Dong ho fd.ms() va hoat hinh dung khi tam dung (hop thoai, bang nhanh).
 *   4.9.6: fd.ipa(i) phien am IPA cua tu i (kho tu may, "" neu khong co) | fd.sub(id, chu) dong phu nho duoi chu
 *     chinh (Noto 14, du glyph IPA). Firmware khong con tro choi dung san: moi tro choi la goi Berry tai tu may chu.
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
#define FD_BERRY_SUB       64
#define FD_BERRY_MEM_CAP   (512 * 1024)
#define FD_BERRY_FRAME_MS  40
#define FD_BERRY_INIT_MS   1500
#define FD_BERRY_TICK_MS   33

typedef struct {
    const char *en;
    const char *vi;
    const char *ipa;            /* 4.9.6, co the rong */
} fd_berry_word_t;

/* 4.9.5: tuy chon cho script (fd.opt) */
typedef struct {
    int dir, spd, rep;                              /* cai dat nguoi choi */
    int fall_ms, min_ms, every, ramp_pct, lives;    /* config goi (hoac mac dinh) */
} fd_berry_opts_t;

/* Do kich thuoc chu (chay trong task VM; ham phai tu khoa LVGL, that bai thi tra false). */
typedef bool (*fd_berry_measure_fn)(const char *txt, int font, int maxw, int *w, int *h);

typedef struct {
    char *script;               /* PSRAM, fd_berry nhan quyen so huu (free khi dung) */
    size_t len;
    const fd_berry_word_t *words;   /* chi doc; nguoi goi giu den khi fd_berry_stop xong */
    int nwords;
    int w, h;
    const char *side;
    fd_berry_opts_t opts;
    fd_berry_measure_fn measure;
} fd_berry_cfg_t;

typedef struct {
    bool used, vis;
    int16_t x, y, w, h;
    int32_t bg, fg;             /* bg < 0 = trong suot */
    uint8_t font;               /* 14, 18, 32 */
    uint8_t bw, radius, pad;    /* 4.9.5: vien (px), bo goc, dem trong */
    int32_t bc;                 /* mau vien */
    char txt[FD_BERRY_TXT];
    char sub[FD_BERRY_SUB];     /* 4.9.6 dong phu (IPA) */
} fd_berry_obj_t;

typedef enum { FD_BERRY_IDLE = 0, FD_BERRY_RUNNING, FD_BERRY_OVER, FD_BERRY_ERROR } fd_berry_state_t;

typedef struct {
    fd_berry_state_t state;
    bool hud_dirty;
    int score, lives, level, combo;
    int sound;                  /* -1 khong, 0 sai, 1 dung */
    char say[48];
    int over_score, over_right, over_level;   /* over_right < 0 = khong co */
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
/* 4.9.5 do dac: thoi gian goi ham script (us) va bo nho dinh cua van gan nhat. */
typedef struct { uint32_t calls, max_us, avg_us; size_t peak_mem; } fd_berry_perf_t;
void fd_berry_perf(fd_berry_perf_t *out);

#ifdef __cplusplus
}
#endif
