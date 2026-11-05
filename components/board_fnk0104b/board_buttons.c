#include "board.h"
#include "button_gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "iot_button.h"

static const char *TAG = "board_btn";

static const gpio_num_t s_pins[BOARD_BTN_COUNT] = {BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4};
static button_handle_t s_btn[BOARD_BTN_COUNT];
static board_btn_cb_t s_cb;
static void *s_ctx;

/* 2.2.5: chong nhan "ma". Tren may that, bam 1 nut bat ky lam IO21 (chi so 0) cung xuong muc thap trong cung khoanh
 * khac (phan mem tu GPIO -> iot_button -> FD_EVT_BUTTON -> UI da kiem tra: moi nut 1 handle, usr_data = chi so rieng,
 * khong co cho nao phat su kien chi so 0) -> do ghep dien tren day IO21 (pull-up trong ~45 kOhm, yeu).
 * Xu ly: luu thoi diem PRESS_DOWN; nut nao xuong trong GHOST_US sau mot nut khac thi coi la ma (bo ca chu ky nhan do);
 * neu 2 nut xuong gan nhu cung luc ma mot trong hai la IO21 -> bo IO21. Log INFO muc ca 4 chan moi lan nhan de chan doan. */
#define GHOST_US 120000
static int64_t s_down_us[BOARD_BTN_COUNT];
static bool s_ghost[BOARD_BTN_COUNT];

static void on_down(void *h, void *arg)
{
    (void)h;
    int i = (int)(intptr_t)arg;
    int64_t now = esp_timer_get_time();
    s_down_us[i] = now;
    s_ghost[i] = false;
    for (int j = 0; j < BOARD_BTN_COUNT; j++) {
        if (j == i || !s_down_us[j] || now - s_down_us[j] > GHOST_US) continue;
        if (gpio_get_level(s_pins[j]) != 0) continue;   /* nut kia da nha -> khong trung */
        if (i == 0 || j != 0) s_ghost[i] = true;        /* nut sau (hoac IO21) la ma */
        else s_ghost[0] = true;                         /* IO21 xuong truoc mot chut, nut i that -> bo IO21 */
    }
    ESP_LOGI(TAG, "down idx=%d gpio=%d | lvl 21=%d 2=%d 3=%d 14=%d%s", i, s_pins[i], gpio_get_level(BOARD_BTN1),
             gpio_get_level(BOARD_BTN2), gpio_get_level(BOARD_BTN3), gpio_get_level(BOARD_BTN4), s_ghost[i] ? " (ma, bo qua)" : "");
}

static void dispatch(void *handle, int index, board_btn_evt_t evt)
{
    (void)handle;
    if (index < 0 || index >= BOARD_BTN_COUNT) return;
    if (s_ghost[index]) { ESP_LOGW(TAG, "bo su kien ma idx=%d gpio=%d evt=%d", index, s_pins[index], (int)evt); return; }
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
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_PRESS_DOWN, NULL, on_down, arg), TAG, "cb");
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_SINGLE_CLICK, NULL, on_click, arg), TAG, "cb");
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_DOUBLE_CLICK, NULL, on_double, arg), TAG, "cb");
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_btn[i], BUTTON_LONG_PRESS_START, NULL, on_long, arg), TAG, "cb");
    }
    ESP_LOGI(TAG, "4 nut: GPIO %d %d %d %d (active-low)", BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4);
    return ESP_OK;
}
