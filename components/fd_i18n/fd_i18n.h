#pragma once
/* Ngon ngu giao dien (vi/en) tren nen lv_i18n. Chuoi giao dien: _("SETTINGS") (khoa tieng Anh VIET_HOA, xem i18n/vi.yml, en.yml) -> ban dich theo ngon ngu hien tai.
 * Ngon ngu luu NVS fd/lang ("vi" | "en"), mac dinh "vi". */
#include "lv_i18n.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FD_LANG_VI = 0, FD_LANG_EN, FD_LANG_COUNT } fd_lang_t;

/* Goi 1 lan luc khoi dong, sau nvs_flash_init va TRUOC khi dung UI: lv_i18n_init + locale da luu. */
void fd_i18n_init(void);
fd_lang_t fd_i18n_get_lang(void);
/* Doi ngon ngu + luu NVS. Man hinh can dung lai de cap nhat chu (ui_nav_refresh). */
void fd_i18n_set_lang(fd_lang_t lang);
/* Ten ngon ngu bang chinh ngon ngu do ("Tieng Viet" / "English"), khong dich. */
const char *fd_i18n_lang_name(fd_lang_t lang);

#ifdef __cplusplus
}
#endif
