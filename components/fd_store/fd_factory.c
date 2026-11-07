/* Khoi phuc cai dat goc.
 * UI chi ghi co NVS fd/factory roi khoi dong lai. Lan boot sau, truoc khi i18n/Wi-Fi/man hinh
 * doc bat ky khoa nao: format phan vung storage (/data) roi xoa toan bo NVS.
 * Xoa NVS = dang xuat (fdict/device_token, user_name), Wi-Fi da luu, PIN, ngon ngu,
 * do sang, am luong, xoay, den trang, thoi gian tat man, che do nha phat trien, cau hinh hoc.
 * Khong dung phan vung voice (Pico), otadata, hay firmware.
 * Bao hanh nam o may chu, khoa theo MAC (eFuse). Xoa NVS khong doi ngay kich hoat. */
#include "fd_store.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "fd_factory";
#define NS  "fd"
#define KEY "factory"

bool fd_factory_arm(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_u8(h, KEY, 1);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "khong ghi duoc co: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

void fd_factory_boot(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_u8(h, KEY, &v);
    nvs_close(h);
    if (v != 1) return;

    ESP_LOGW(TAG, "khoi phuc cai dat goc");
    esp_err_t fe = fd_store_format();
    if (fe != ESP_OK) ESP_LOGE(TAG, "format /data: %s", esp_err_to_name(fe));

    nvs_flash_deinit();
    esp_err_t ee = nvs_flash_erase();
    if (ee != ESP_OK) {
        ESP_LOGE(TAG, "xoa NVS: %s", esp_err_to_name(ee));
        ESP_ERROR_CHECK(nvs_flash_init());
        return;   /* co con lai -> lan khoi dong sau thu lai */
    }
    ESP_ERROR_CHECK(nvs_flash_init());
    if (fe != ESP_OK) fd_factory_arm();   /* NVS sach nhung /data chua xoa -> format lai lan sau */
}
