/* WS2812B (GPIO42) - LED NAY CO Y LUON TAT (khong bao gio bat). Firmware khong dung LED nay; chi gui 1 khung GRB 0,0,0 de tat mau "rac" bi chot
 * (nhieu tren GPIO42/MTMS luc cap nguon/reset). RMT TX tam thoi -> giai phong ngay, giu chan muc thap. */
#include "board.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

#define RGB_RES_HZ 10000000   /* 10 MHz: 1 tick = 0.1 us */

void board_rgb_off(bool hold_for_sleep)
{
    gpio_hold_dis(BOARD_RGB_LED);   /* sau deep sleep chan dang bi giu -> nha de RMT dieu khien */
    static const uint8_t grb[3] = {0, 0, 0};
    rmt_channel_handle_t ch = NULL;
    rmt_encoder_handle_t enc = NULL;
    rmt_tx_channel_config_t cc = {
        .gpio_num = BOARD_RGB_LED,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RGB_RES_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 1,
    };
    /* WS2812B: bit0 = 0.4 us cao + 0.85 us thap, bit1 = 0.8 us cao + 0.45 us thap, MSB truoc */
    rmt_bytes_encoder_config_t ec = {
        .bit0 = {.level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 9},
        .bit1 = {.level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 5},
        .flags.msb_first = 1,
    };
    rmt_transmit_config_t tc = {.loop_count = 0};
    esp_err_t err = rmt_new_tx_channel(&cc, &ch);
    if (err == ESP_OK) err = rmt_new_bytes_encoder(&ec, &enc);
    if (err == ESP_OK) err = rmt_enable(ch);
    if (err == ESP_OK) err = rmt_transmit(ch, enc, grb, sizeof(grb), &tc);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(ch, 100);
    if (err != ESP_OK) ESP_LOGW("board_rgb", "tat WS2812 loi: %s", esp_err_to_name(err));
    if (ch) {
        rmt_disable(ch);
        rmt_del_channel(ch);
    }
    if (enc) rmt_del_encoder(enc);
    /* chan o muc thap (>= reset 280 us) de LED chot khung 0 va khong nhan nhieu */
    gpio_reset_pin(BOARD_RGB_LED);
    gpio_set_direction(BOARD_RGB_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_RGB_LED, 0);
    if (hold_for_sleep) {   /* giu muc thap suot deep sleep (GPIO42 la chan so, khong phai RTC) */
        gpio_hold_en(BOARD_RGB_LED);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
        gpio_deep_sleep_hold_en();
#endif
    }
}