#include <stdio.h>
/*
 * Fdict firmware - diem vao.
 * Che do chon trong menuconfig: Fdict -> Che do firmware (Ung dung / Kiem thu phan cung).
 */
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "board.h"
#include "fd_batt.h"
#include "fd_deck.h"
#include "fd_display.h"
#include "fd_events.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_store.h"
#include "fd_ui.h"
#include "sys_info.h"

static const char *TAG = "app";

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void on_board_button(int index, board_btn_evt_t evt, void *ctx)
{
    (void)ctx;
    fd_evt_button_t e = {.index = index, .action = (fd_btn_action_t)evt};
    fd_events_post(FD_EVT_BUTTON, &e, sizeof(e));
}

static void run_app(void)
{
    ESP_ERROR_CHECK(fd_events_init());

    bool storage_ok = fd_store_mount() == ESP_OK && fd_store_selftest() == ESP_OK;   /* tu kiem tra OTA */

#if CONFIG_FD_WIFI_INIT_BEFORE_DISPLAY
    ESP_LOGI(TAG, "boot order: wifi first");
    ESP_ERROR_CHECK(fd_net_init());          /* UI doc fd_net_state() khi build nen van dung trang thai */
    ESP_ERROR_CHECK(fd_display_init());
#else
    ESP_LOGI(TAG, "boot order: display first");
    ESP_ERROR_CHECK(fd_display_init());
    ESP_ERROR_CHECK(fd_net_init());          /* tu ket noi mang da luu (neu co) */
#endif
    ESP_ERROR_CHECK(fd_ui_start(FD_SCR_HOME)); /* luon vao Home; WiFi nam trong Cai dat */
    board_rgb_off(false);   /* WS2812 co y luon tat (ca khi thuc tu deep sleep): xoa mau rac chot luc cap nguon */
    ESP_ERROR_CHECK(board_buttons_init(on_board_button, NULL));
    if (fd_batt_init() != ESP_OK) ESP_LOGW("main", "fd_batt_init loi -> an % pin");

    /* OTA: neu PENDING_VERIFY -> tu kiem tra (display + storage + WiFi trong 60 s) roi mark valid / rollback
     * (thay mark_app_valid() vo dieu kien truoc day). Display loi da abort o tren -> bootloader rollback. */
    if (fd_ota_start(true, storage_ok) != ESP_OK) ESP_LOGW(TAG, "fd_ota_start loi");
    if (storage_ok) fd_deck_init();   /* 23/10: task bo tu offline chay tu boot -> tu dong bo khi WiFi len, gui outbox */
    sys_info_log_heap();

#if CONFIG_FD_HEAP_LOG_PERIOD_S > 0
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_FD_HEAP_LOG_PERIOD_S * 1000));
        sys_info_log_heap();
    }
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "firmware: %s", FD_FW_NAME);
    { char sha[11]; esp_app_get_elf_sha256(sha, sizeof(sha)); ESP_LOGI(TAG, "build: %s", sha); }
    {   /* eFuse OPTIONAL_UNIQUE_ID (128 bit, khong bi mat): chi log, chua dung */
        uint8_t uid[16] = {0};
        esp_err_t ue = esp_efuse_read_field_blob(ESP_EFUSE_OPTIONAL_UNIQUE_ID, uid, 128);
        bool nz = false;
        char hx[33];
        for (int i = 0; i < 16; i++) { nz |= uid[i] != 0; snprintf(hx + i * 2, 3, "%02x", uid[i]); }
        ESP_LOGI(TAG, "efuse optional_unique_id: %s, %s, %s", esp_err_to_name(ue), nz ? "da ghi" : "chua ghi (toan 0)", hx);
    }
    nvs_init();
    sys_info_print();

    run_app();
}
