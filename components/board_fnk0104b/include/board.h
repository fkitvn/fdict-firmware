#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "board_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bus I2C dung chung (tao mot lan). */
esp_err_t board_i2c_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);
/* Tat WS2812B (GPIO42): gui 1 khung GRB 0,0,0 qua RMT tam thoi roi giai phong, giu chan muc thap. LED co y LUON TAT.
 * hold_for_sleep: giu GPIO42 muc thap suot deep sleep (goi ngay truoc esp_deep_sleep_start). */
void board_rgb_off(bool hold_for_sleep);

/* LCD: SPI bus + panel ILI9341. Backlight tat cho den khi goi board_backlight_set(). */
esp_err_t board_lcd_init(esp_lcd_panel_io_handle_t *out_io, esp_lcd_panel_handle_t *out_panel);
esp_err_t board_backlight_set(int percent);

/* Touch FT6336U (driver ft5x06). Goi board_i2c_init() truoc. */
esp_err_t board_touch_init(esp_lcd_touch_handle_t *out_touch);

/* Nut bam */
typedef enum {
    BOARD_BTN_EVT_CLICK = 0,
    BOARD_BTN_EVT_DOUBLE_CLICK,
    BOARD_BTN_EVT_LONG_PRESS,
} board_btn_evt_t;

typedef void (*board_btn_cb_t)(int btn_index, board_btn_evt_t evt, void *ctx);
esp_err_t board_buttons_init(board_btn_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
