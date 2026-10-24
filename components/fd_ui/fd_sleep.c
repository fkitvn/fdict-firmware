/* Ngu (deep sleep; board khong co mach giu nguon/latch): dung WiFi, tat am thanh, tat LCD, deep sleep.
 * Danh thuc bang nut bat ky (GPIO21/2/3/14, active-low, RTC GPIO) HOAC cham man hinh (FT6336U INT = GPIO17,
 * active-low, RTC GPIO) qua ext1 -> khoi dong lai binh thuong (co PIN + co fd/pin_req -> man PIN).
 * Cat han pin can cong tac / mach latch phan cung. */
#include "board.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "fd_audio.h"
#include "fd_display.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_priv.h"

static const char *TAG = "fd_sleep";

/* Cam ung FT6336U truoc deep sleep: KHONG hibernate (0xA5=3 tat INT). Dat G_MODE (0xA4) = 0 polling (INT giu thap
 * suot luc cham, de ext1 bat muc thap chac chan) va CTRL (0x86) = 1 (tu vao Monitor khi khong cham).
 * Giu RST muc cao qua deep sleep (gpio_hold_en; board_touch_init nha lai). Cho nha tay (INT cao) de khong bi danh
 * thuc ngay (toi da 10 s). Thiet bi I2C tam (0x38) duoc go ngay sau khi ghi. */
static void touch_prepare_wake(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (bus) {
        i2c_master_dev_handle_t dev = NULL;
        i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x38, .scl_speed_hz = 100000};
        if (i2c_master_bus_add_device(bus, &dc, &dev) == ESP_OK) {
            const uint8_t g_mode[2] = {0xA4, 0x00};
            const uint8_t ctrl[2] = {0x86, 0x01};
            esp_err_t e1 = i2c_master_transmit(dev, g_mode, sizeof(g_mode), 50);
            esp_err_t e2 = i2c_master_transmit(dev, ctrl, sizeof(ctrl), 50);
            ESP_LOGI(TAG, "touch: polling INT %s, auto monitor %s", esp_err_to_name(e1), esp_err_to_name(e2));
            i2c_master_bus_rm_device(dev);
        }
    }
    gpio_set_level(BOARD_TOUCH_RST, 1);
    gpio_hold_en(BOARD_TOUCH_RST);
    for (int t = 0; t < 500 && gpio_get_level(BOARD_TOUCH_INT) == 0; t++) vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDelay(pdMS_TO_TICKS(30));   /* qua chu ky bao cao cuoi */
    ESP_LOGI(TAG, "touch INT=%d truoc khi ngu", gpio_get_level(BOARD_TOUCH_INT));
}

void fd_sleep_enter(void)
{
    fd_ota_selftest_wait(60000);   /* deep sleep = khoi dong lai: dang PENDING_VERIFY se bi bootloader rollback */
    esp_err_t e = esp_wifi_stop();   /* loi neu WiFi chua khoi dong -> bo qua */
    ESP_LOGI(TAG, "wifi stop: %s", esp_err_to_name(e));
    fd_audio_power_off();
    fd_display_power_down();
    board_rgb_off(true);   /* WS2812 tat + giu GPIO42 thap suot deep sleep */
    /* NVS: moi lan ghi trong fw deu nvs_commit ngay -> khong con du lieu cho */
    ESP_LOGI(TAG, "sleep: enter (deep sleep)");

    /* cho nha CA 4 nut de khong bi danh thuc ngay (toi da 10 s) + chong doi phim */
    static const gpio_num_t BTN[4] = {BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4};
    for (int t = 0; t < 500; t++) {
        bool any = false;
        for (int k = 0; k < 4; k++) any |= gpio_get_level(BTN[k]) == 0;
        if (!any) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    touch_prepare_wake();

    /* nut bat ky (active-low) danh thuc qua ext1; giu mien RTC periph de pull-up RTC con hieu luc */
    uint64_t mask = 0;
    for (int k = 0; k < 4; k++) {
        rtc_gpio_pullup_en(BTN[k]);
        rtc_gpio_pulldown_dis(BTN[k]);
        mask |= 1ULL << BTN[k];
    }
    rtc_gpio_pullup_en(BOARD_TOUCH_INT);   /* cham man hinh (INT active-low) cung danh thuc */
    rtc_gpio_pulldown_dis(BOARD_TOUCH_INT);
    mask |= 1ULL << BOARD_TOUCH_INT;
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);    esp_deep_sleep_start();
}