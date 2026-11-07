#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define FD_STORE_BASE_PATH  "/data"
#define FD_STORE_PARTITION  "storage"

#ifdef __cplusplus
extern "C" {
#endif

/* Mount LittleFS tai /data (tu format lan dau neu mount loi). */
esp_err_t fd_store_mount(void);
esp_err_t fd_store_info(size_t *total, size_t *used);
/* Ghi/doc/xoa file thu /data/.selftest, tang bo dem /data/boot_count.txt. */
esp_err_t fd_store_selftest(void);
/* Xoa toan bo phan vung /data. Goi khi chua mount. Khong dung phan vung voice. */
esp_err_t fd_store_format(void);

/* Dat co NVS. Lan khoi dong sau (fd_factory_boot) format /data va xoa het NVS. */
bool fd_factory_arm(void);
/* Goi ngay sau nvs_flash_init(), truoc i18n / mount / Wi-Fi / man hinh. */
void fd_factory_boot(void);

#ifdef __cplusplus
}
#endif
