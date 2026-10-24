#include "fd_display.h"
#include "board.h"
#include "board_orient.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "fd_display";

#define DRAW_BUF_LINES BOARD_DRAW_BUF_LINES

static lv_display_t *s_disp;
static lv_indev_t *s_touch;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_touch_handle_t s_tp;
/* Huong chi doi khi nguoi dung tu xoay (Cai dat > Huong xoay man hinh) va duoc luu o NVS fd/rot4.
 * Chua luu -> FD_ROT_DEFAULT_IDX = 2 (ngang 180 do, huong cam binh thuong cua may; truoc 17:15 la 0 do =
 * BOARD_ORIENT_DEFAULT_IDX). Lua chon da luu cua nguoi dung luon thang. LCD (lvgl port cfg) + touch (apply_hw
 * sau board_touch_init) cung dung huong nay ngay tu fd_display_init. */
#define NVS_NS  "fd"
#define NVS_ROT "rot4"
#define FD_ROT_DEFAULT_IDX 2   /* 180 do */
static int s_rot = FD_ROT_DEFAULT_IDX;
#define FD_ROT_ALLOWED(i) ((unsigned)(i) < BOARD_ORIENT_COUNT)   /* 0/90/180/270 (bang BOARD_ORIENTS) */

static int load_rotation(void)
{
    nvs_handle_t h;
    uint8_t v = 0xFF;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_ROT, &v);
        nvs_close(h);
    }
    /* Chua luu -> mac dinh 180 do. Da luu 0..3 -> dung lua chon do. Gia tri sai -> mac dinh va ghi lai NVS. */
    if (v == 0xFF) return FD_ROT_DEFAULT_IDX;
    if (FD_ROT_ALLOWED(v)) return v;
    ESP_LOGW(TAG, "rot4=%u khong hop le -> %d (180 do)", v, FD_ROT_DEFAULT_IDX);
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_ROT, FD_ROT_DEFAULT_IDX);
        nvs_commit(h);
        nvs_close(h);
    }
    return FD_ROT_DEFAULT_IDX;
}

#define NVS_BRIGHT "bright"   /* do sang 10..100 %, mac dinh CONFIG_BOARD_BACKLIGHT_DEFAULT */
static int s_bright = CONFIG_BOARD_BACKLIGHT_DEFAULT;
static bool s_asleep;   /* man hinh tat (het thoi gian cho): doi do sang chi luu, khong bat den */

static int load_brightness(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_BRIGHT, &v);
        nvs_close(h);
    }
    return (v >= 10 && v <= 100) ? v : CONFIG_BOARD_BACKLIGHT_DEFAULT;
}

void fd_display_save_brightness(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_BRIGHT, (uint8_t)s_bright);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void save_rotation(int idx)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_ROT, (uint8_t)idx);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* Ap dung MADCTL + co touch */
static void apply_hw(const board_orient_t *o)
{
    esp_lcd_panel_swap_xy(s_panel, o->lcd_swap_xy);
    esp_lcd_panel_mirror(s_panel, o->lcd_mirror_x, o->lcd_mirror_y);
    if (s_tp) {
        esp_lcd_touch_set_swap_xy(s_tp, o->tp_swap_xy);
        esp_lcd_touch_set_mirror_x(s_tp, o->tp_mirror_x);
        esp_lcd_touch_set_mirror_y(s_tp, o->tp_mirror_y);
    }
}

esp_err_t fd_display_init(void)
{
    s_rot = load_rotation();
    const board_orient_t *o = &BOARD_ORIENTS[s_rot];

    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(board_lcd_init(&io, &s_panel), TAG, "lcd");
    s_io = io;

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = 1;          /* UI chay rieng core 1 */
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl_port_init");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = s_panel,
        /* Cap cho dong dai nhat (320 px) -> du cho ca ngang va doc: 2 x 25.6KB internal DMA */
        .buffer_size = BOARD_DISP_MAX_LINE_PX * DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = o->h_res,
        .vres = o->v_res,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            /* Xoay phan cung (MADCTL). esp_lvgl_port ap lai gia tri nay khi doi do phan giai,
             * nen fd_display_set_rotation() ghi de MADCTL ngay sau lv_display_set_resolution(). */
            .swap_xy = o->lcd_swap_xy,
            .mirror_x = o->lcd_mirror_x,
            .mirror_y = o->lcd_mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,   /* ILI9341 SPI nhan RGB565 big-endian */
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "lvgl_port_add_disp");

    esp_err_t err = board_touch_init(&s_tp);
    if (err == ESP_OK) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = s_disp,
            .handle = s_tp,
        };
        s_touch = lvgl_port_add_touch(&touch_cfg);
    } else {
        s_tp = NULL;
        ESP_LOGW(TAG, "Khong khoi tao duoc touch: %s (UI van chay)", esp_err_to_name(err));
    }
    apply_hw(o);

    s_bright = load_brightness();
    board_backlight_set(s_bright);
    ESP_LOGI(TAG, "LVGL %d.%d.%d san sang %dx%d (xoay %d do), draw buf 2x(%dx%d) DMA internal, task core 1",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, o->h_res, o->v_res, o->deg,
             BOARD_DISP_MAX_LINE_PX, DRAW_BUF_LINES);
    return ESP_OK;
}

esp_err_t fd_display_set_rotation(int idx, bool persist)
{
    ESP_RETURN_ON_FALSE(s_disp && FD_ROT_ALLOWED(idx), ESP_ERR_INVALID_ARG, TAG, "rot");   /* 0..3 */
    const board_orient_t *o = &BOARD_ORIENTS[idx];
    s_rot = idx;
    /* Callback RESOLUTION_CHANGED cua esp_lvgl_port ghi MADCTL theo cau hinh luc init,
     * vi vay ap dung MADCTL cua minh SAU khi doi do phan giai. */
    lv_display_set_resolution(s_disp, o->h_res, o->v_res);
    apply_hw(o);
    lv_obj_invalidate(lv_screen_active());
    if (persist) save_rotation(idx);
    return ESP_OK;
}

int fd_display_get_rotation(void) { return s_rot; }
int fd_display_rotation_deg(int idx) { return (idx >= 0 && idx < BOARD_ORIENT_COUNT) ? BOARD_ORIENTS[idx].deg : -1; }
lv_display_t *fd_display_get(void) { return s_disp; }
lv_indev_t *fd_display_touch_indev(void) { return s_touch; }
bool fd_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void fd_display_unlock(void) { lvgl_port_unlock(); }
esp_err_t fd_display_set_brightness(int percent)
{
    s_bright = percent < 10 ? 10 : percent > 100 ? 100 : percent;   /* khong bao gio tat han */
    return s_asleep ? ESP_OK : board_backlight_set(s_bright);
}

void fd_display_set_sleep(bool sleep)
{
    s_asleep = sleep;
    board_backlight_set(sleep ? 0 : s_bright);
}
bool fd_display_is_asleep(void) { return s_asleep; }

/* Tat nguon mem: den nen tat, LCD Display OFF (0x28) roi Sleep IN (0x10). Goi khi dang giu khoa LVGL. */
void fd_display_power_down(void)
{
    s_asleep = true;
    board_backlight_set(0);
    if (s_io) {
        esp_lcd_panel_io_tx_param(s_io, 0x28, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        esp_lcd_panel_io_tx_param(s_io, 0x10, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
int fd_display_get_brightness(void) { return s_bright; }
