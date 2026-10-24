#include "board.h"
#include "esp_log.h"

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    ESP_LOGI(TAG, "I2C bus SCL=%d SDA=%d: %s", BOARD_I2C_SCL, BOARD_I2C_SDA, esp_err_to_name(err));
    return err;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c_bus;
}
