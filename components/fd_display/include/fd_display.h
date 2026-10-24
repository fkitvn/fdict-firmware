#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Khoi tao LCD + touch + esp_lvgl_port (task LVGL tren core 1).
 * Huong lay tu NVS (fd/rot4); chua luu thi = huong ngang da kiem chung (0 do). Can nvs_flash_init() truoc. */
esp_err_t fd_display_init(void);
lv_display_t *fd_display_get(void);
lv_indev_t *fd_display_touch_indev(void);

/* Phai bao moi truy cap LVGL ngoai task LVGL bang lock/unlock. */
bool fd_display_lock(uint32_t timeout_ms);
void fd_display_unlock(void);
/* Do sang 10..100 % (ap dung ngay, chua luu). Luc init lay tu NVS fd/bright. */
esp_err_t fd_display_set_brightness(int percent);
int fd_display_get_brightness(void);
/* Luu do sang hien tai vao NVS (goi khi nha tay, tranh ghi flash lien tuc). */
void fd_display_save_brightness(void);
/* Tat/bat den nen (het thoi gian cho). Khi dang tat, set_brightness chi ghi nho gia tri. */
void fd_display_set_sleep(bool sleep);
bool fd_display_is_asleep(void);
/* Tat nguon mem: den nen + LCD 0x28/0x10 (chi dung truoc deep sleep). */
void fd_display_power_down(void);

/* Xoay luc chay: idx 0..3 = 0/90/180/270 do (xem board_orient.h). GOI KHI DANG GIU LOCK.
 * Doi MADCTL + co touch + do phan giai LVGL; persist=true thi luu NVS (fd/rot4). */
esp_err_t fd_display_set_rotation(int idx, bool persist);
int fd_display_get_rotation(void);
int fd_display_rotation_deg(int idx);

#ifdef __cplusplus
}
#endif
