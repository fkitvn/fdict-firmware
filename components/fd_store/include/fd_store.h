#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define FD_STORE_BASE_PATH  "/data"
#define FD_STORE_PARTITION  "storage"

#ifdef __cplusplus
extern "C" {
#endif

/* Mount without formatting, even on first boot. Failed data is preserved. */
esp_err_t fd_store_mount(void);
esp_err_t fd_store_info(size_t *total, size_t *used);
esp_err_t fd_store_status(void);   /* last mount/self-test result for UI diagnostics */
/* Ghi/doc/xoa file thu /data/.selftest, tang bo dem /data/boot_count.txt. */
esp_err_t fd_store_selftest(void);

/* Dat co NVS. Lan khoi dong sau (fd_factory_boot) format /data va xoa het NVS. */
bool fd_factory_arm(void);
/* Goi ngay sau nvs_flash_init(), truoc i18n / mount / Wi-Fi / man hinh. */
void fd_factory_boot(void);

#ifdef __cplusplus
}
#endif
