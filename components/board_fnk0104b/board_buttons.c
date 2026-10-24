#include "board.h"
#include "button_gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "iot_button.h"

static const char *TAG = "board_btn";

static const gpio_num_t s_pins[BOARD_BTN_COUNT] = {BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4};
static button_handle_t s_btn[BOARD_BTN_COUNT];
static board_btn_cb_t s_cb;
static void *s_ctx;

static void dispatch(void *handle, int index, board_btn_evt_t evt)
{
    (void)handle;
    if (s_cb) {
        s_cb(index, evt, s_ctx);
    }
}

static void on_click(void *h, void *arg)  { dispatch(h, (int)(intptr_t)arg, BOARD_BTN_EVT_CLICK); }
static void on_double(void *h, void *arg) { dispatch(h, (int)(intptr_t)arg, BOARD_BTN_EVT_DOUBLE_CLICK); }
static void on_long(void *h, void *arg)   { dispatch(h, (int)(intptr_t)arg, BOARD_BTN_EVT_LONG_PRESS); }

esp_err_t board_buttons_init(board_btn_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
    const button_config_t btn_cfg = {
        .long_press_time = 800,
        .short_press_time = 180,
    };
    /* Sau deep sleep (tat nguon mem): ghi nguyen nhan danh thuc, tra cac chan ve GPIO thuong (bo RTC IO / pull-up RTC) */
    if (esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_EXT1)) {
        uint64_t st = esp_sleep_get_ext1_wakeup_status();
        int btn = 0;
        for (int i = 0; i < BOARD_BTN_COUNT; i++) {
            if (st & (1ULL << s_pins[i])) btn = i + 1;
        }
        ESP_LOGI(TAG, "wake: button %d (ext1 0x%llx)", btn, (unsigned long long)st);
    }
    for (int i = 0; i < BOARD_BTN_COUNT; i++) {
        if (rtc_gpio_is_valid_gpio(s_pins[i])) rtc_gpio_deinit(s_pins[i]);
    }
    for (int i = 0; i < BOARD_BTN_COUNT; i++) {
        const button_gpio_config_t gpio_cfg = {
            .gpio_num = s_pins[i],
            .active_level = 0,
        };
        ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &s_btn[i]), TAG, "btn %d", i);
        void *arg = (void *)(intptr_t)i;
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_SINGLE_CLICK, NULL, on_click, arg), TAG, "cb");
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_DOUBLE_CLICK, NULL, on_double, arg), TAG, "cb");
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_LONG_PRESS_START, NULL, on_long, arg), TAG, "cb");
    }
    ESP_LOGI(TAG, "4 nut: GPIO %d %d %d %d (active-low)", BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4);
    return ESP_OK;
}
