#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Khoi dong ADC + timer doc pin moi 180 s (doc lan dau ngay). Khong lam gi neu FD_BATTERY_ENABLE=n. */
esp_err_t fd_batt_init(void);
/* % pin da lam muot (0..100), -1 neu tat / chua doc duoc. */
int fd_batt_get_pct(void);
/* Dien ap pin (mV, da nhan he so chia), -1 neu tat / chua doc duoc. */
int fd_batt_get_mv(void);

#ifdef __cplusplus
}
#endif