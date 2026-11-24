/* Ngu (deep sleep; board khong co mach giu nguon/latch): dung WiFi, tat am thanh, tat LCD, deep sleep.
 * Danh thuc bang nut bat ky (GPIO21/2/3/14, active-low) qua ext1, HOAC cham man hinh hai lan.
 * Cham don khong bat may: ULP RISC-V dem hai lan INT thap (FT6336U GPIO17) roi moi goi CPU.
 * Khoi dong lai binh thuong (co PIN + co fd/pin_req -> man PIN). */
#include <stddef.h>
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
#include "ulp_riscv.h"
#include "ulp_riscv_gpio.h"

extern const uint8_t ulp_tap_bin_start[] asm("_binary_ulp_tap_bin_start");
extern const uint8_t ulp_tap_bin_end[] asm("_binary_ulp_tap_bin_end");

static const char *TAG = "fd_sleep";

/* Cam ung FT6336U truoc deep sleep: KHONG hibernate (0xA5=3 tat INT). Dat G_MODE (0xA4) = 0 polling (INT giu thap
 * suot luc cham, de ULP thay mot lan cham) va CTRL (0x86) = 1 (tu vao Monitor khi khong cham).
 * Giu RST muc cao qua deep sleep (gpio_hold_en; board_touch_init nha lai). Cho nha tay (INT cao) de lan cham cu
 * khong tinh la lan thu nhat. Thiet bi I2C tam (0x38) duoc go ngay sau khi ghi. */
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
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    /* Cham hai lan: ULP doc INT. Mot lan cham khong dua GPIO17 vao ext1. */
    ulp_riscv_timer_stop();
    ulp_riscv_halt();
    ulp_riscv_gpio_init(BOARD_TOUCH_INT);
    ulp_riscv_gpio_input_enable(BOARD_TOUCH_INT);
    ulp_riscv_gpio_pullup(BOARD_TOUCH_INT);
    ulp_riscv_gpio_pulldown_disable(BOARD_TOUCH_INT);
    size_t ulp_bytes = (size_t)(ulp_tap_bin_end - ulp_tap_bin_start);
    esp_err_t ue = ulp_riscv_load_binary(ulp_tap_bin_start, ulp_bytes);
    if (ue == ESP_OK) ue = ulp_riscv_run();
    if (ue == ESP_OK) ue = esp_sleep_enable_ulp_wakeup();
    ESP_LOGI(TAG, "ulp double-tap: %s (%u B)", esp_err_to_name(ue), (unsigned)ulp_bytes);
    esp_deep_sleep_start();
}

void fd_sleep_after_wake(void)
{
    uint32_t cause = esp_sleep_get_wakeup_causes();
    if ((cause & ((1u << ESP_SLEEP_WAKEUP_ULP) | (1u << ESP_SLEEP_WAKEUP_EXT1))) == 0) return;
    ulp_riscv_timer_stop();
    ulp_riscv_halt();
    if (rtc_gpio_is_valid_gpio(BOARD_TOUCH_INT)) rtc_gpio_deinit(BOARD_TOUCH_INT);
}