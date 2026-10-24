#pragma once
/*
 * Huong hien thi -> tham so LCD (MADCTL qua esp_lcd swap_xy/mirror) va cam ung (esp_lcd_touch).
 *
 * Toa do tho FT6336U (tx 0..239, ty 0..319) trung voi huong doc da kiem chung tren board
 * (LCD mirror_x=1, mirror_y=0, touch khong swap/mirror).
 * esp_lcd_touch ap dung mirror (theo x_max=240, y_max=320) TRUOC roi moi swap.
 *
 * | Huong              | LCD swap/mx/my | Touch swap/mx/my | Anh xa                      |
 * |--------------------|----------------|------------------|-----------------------------|
 * | Doc                | 0/1/0          | 0/0/0            | x=tx,      y=ty             |
 * | Doc + 180          | 0/0/1          | 0/1/1            | x=239-tx,  y=319-ty         |
 * | Ngang (xiaozhi)    | 1/0/0          | 1/1/0            | x=ty,      y=239-tx         |
 * | Ngang + 180        | 1/1/1          | 1/0/1            | x=319-ty,  y=tx             |
 *
 * Ngang = MADCTL MV (swap_xy=true, khong mirror) giong xiaozhi-esp32
 * boards/freenove-esp32s3-display-2.8-lcd/config.h. xiaozhi khong dung toa do cam ung
 * nen tham so touch ngang duoc suy ra tu MADCTL (can xac nhan tren board).
 */
#include "sdkconfig.h"
#include "board_pins.h"

#ifndef CONFIG_BOARD_ROTATE_180
#define CONFIG_BOARD_ROTATE_180 0
#endif

#if CONFIG_BOARD_ORIENTATION_LANDSCAPE
#define BOARD_DISP_H_RES    BOARD_LCD_V_RES   /* 320 */
#define BOARD_DISP_V_RES    BOARD_LCD_H_RES   /* 240 */
#define BOARD_LCD_SWAP_XY   1
#if CONFIG_BOARD_ROTATE_180
#define BOARD_LCD_MIRROR_X  1
#define BOARD_LCD_MIRROR_Y  1
#define BOARD_TOUCH_SWAP_XY 1
#define BOARD_TOUCH_MIRROR_X 0
#define BOARD_TOUCH_MIRROR_Y 1
#else
#define BOARD_LCD_MIRROR_X  0
#define BOARD_LCD_MIRROR_Y  0
#define BOARD_TOUCH_SWAP_XY 1
#define BOARD_TOUCH_MIRROR_X 1
#define BOARD_TOUCH_MIRROR_Y 0
#endif
#else /* portrait */
#define BOARD_DISP_H_RES    BOARD_LCD_H_RES   /* 240 */
#define BOARD_DISP_V_RES    BOARD_LCD_V_RES   /* 320 */
#define BOARD_LCD_SWAP_XY   0
#define BOARD_TOUCH_SWAP_XY 0
#if CONFIG_BOARD_ROTATE_180
#define BOARD_LCD_MIRROR_X  0
#define BOARD_LCD_MIRROR_Y  1
#define BOARD_TOUCH_MIRROR_X 1
#define BOARD_TOUCH_MIRROR_Y 1
#else
#define BOARD_LCD_MIRROR_X  1
#define BOARD_LCD_MIRROR_Y  0
#define BOARD_TOUCH_MIRROR_X 0
#define BOARD_TOUCH_MIRROR_Y 0
#endif
#endif

#define BOARD_DRAW_BUF_LINES 40

/* ---------- Xoay luc chay (Cai dat > Huong xoay man hinh; lcd_test B4) ----------
 * Chi so xoay tinh theo chieu B4: 0 = ngang, 1 = doc, 2 = ngang 180, 3 = doc 180.
 * Gia tri lay dung tu bang tren (ngang + doc da kiem chung tren board).
 */
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t deg;        /* 0/90/180/270 (nhan de log) */
    uint16_t h_res;
    uint16_t v_res;
    bool lcd_swap_xy, lcd_mirror_x, lcd_mirror_y;
    bool tp_swap_xy, tp_mirror_x, tp_mirror_y;
} board_orient_t;

#define BOARD_ORIENT_COUNT 4

static const board_orient_t BOARD_ORIENTS[BOARD_ORIENT_COUNT] = {
    /* deg  w    h     LCD s mx my   TP s mx my */
    {0,   320, 240,  1, 0, 0,       1, 1, 0},   /* ngang (xiaozhi, da kiem chung) */
    {90,  240, 320,  0, 1, 0,       0, 0, 0},   /* doc (da kiem chung) */
    {180, 320, 240,  1, 1, 1,       1, 0, 1},   /* ngang 180 */
    {270, 240, 320,  0, 0, 1,       0, 1, 1},   /* doc 180 */
};

#if CONFIG_BOARD_ORIENTATION_LANDSCAPE
#define BOARD_ORIENT_DEFAULT_IDX (CONFIG_BOARD_ROTATE_180 ? 2 : 0)
#else
#define BOARD_ORIENT_DEFAULT_IDX (CONFIG_BOARD_ROTATE_180 ? 3 : 1)
#endif

/* Kich thuoc lon nhat cua 1 dong (de cap phat buffer/SPI du cho moi huong) */
#define BOARD_DISP_MAX_LINE_PX BOARD_LCD_V_RES   /* 320 */
