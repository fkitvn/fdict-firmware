#include "fd_store.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "fd_store";

esp_err_t fd_store_mount(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = FD_STORE_BASE_PATH,
        .partition_label = FD_STORE_PARTITION,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount LittleFS that bai: %s", esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    fd_store_info(&total, &used);
    ESP_LOGI(TAG, "LittleFS %s (%s): %u KB / %u KB", FD_STORE_BASE_PATH, FD_STORE_PARTITION,
             (unsigned)(used / 1024), (unsigned)(total / 1024));
    return ESP_OK;
}

esp_err_t fd_store_info(size_t *total, size_t *used)
{
    return esp_littlefs_info(FD_STORE_PARTITION, total, used);
}

esp_err_t fd_store_selftest(void)
{
    const char *path = FD_STORE_BASE_PATH "/.selftest";
    const char *msg = "Fdict LittleFS OK - Tiếng Việt ✓";
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "selftest: khong mo duoc %s de ghi", path);
        return ESP_FAIL;
    }
    fputs(msg, f);
    fclose(f);

    char buf[64] = {0};
    f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "selftest: khong mo duoc %s de doc", path);
        return ESP_FAIL;
    }
    fgets(buf, sizeof(buf), f);
    fclose(f);
    unlink(path);
    if (strcmp(buf, msg) != 0) {
        ESP_LOGE(TAG, "selftest: du lieu doc lai khong khop");
        return ESP_FAIL;
    }

    /* Bo dem so lan boot: kiem tra du lieu ton tai qua reset */
    unsigned boots = 0;
    f = fopen(FD_STORE_BASE_PATH "/boot_count.txt", "r");
    if (f) {
        if (fscanf(f, "%u", &boots) != 1) boots = 0;
        fclose(f);
    }
    boots++;
    f = fopen(FD_STORE_BASE_PATH "/boot_count.txt", "w");
    if (f) {
        fprintf(f, "%u\n", boots);
        fclose(f);
    }
    ESP_LOGI(TAG, "selftest OK (ghi/doc/xoa), boot_count=%u", boots);
    return ESP_OK;
}
