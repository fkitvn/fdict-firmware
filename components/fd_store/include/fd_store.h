#pragma once
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

#ifdef __cplusplus
}
#endif
