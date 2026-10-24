#include "board.h"
#include "board_orient.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board_touch";

esp_err_t board_touch_init(esp_lcd_touch_handle_t *out_touch)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c");
    gpio_hold_dis(BOARD_TOUCH_RST);   /* sau deep sleep (cham de danh thuc) RST dang bi giu -> nha de driver reset duoc */

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_cfg, &io), TAG, "touch io");

    esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = BOARD_TOUCH_SWAP_XY,
            .mirror_x = BOARD_TOUCH_MIRROR_X,
            .mirror_y = BOARD_TOUCH_MIRROR_Y,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(io, &cfg, out_touch), TAG, "ft5x06");
    ESP_LOGI(TAG, "FT6336U (ft5x06 driver) @0x38 RST=%d INT=%d swap_xy=%d mirror_x=%d mirror_y=%d", BOARD_TOUCH_RST,
             BOARD_TOUCH_INT, BOARD_TOUCH_SWAP_XY, BOARD_TOUCH_MIRROR_X, BOARD_TOUCH_MIRROR_Y);
    return ESP_OK;
}
