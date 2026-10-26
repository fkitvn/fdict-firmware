#include "fd_i18n.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "fd_i18n";
static const char *const CODE[FD_LANG_COUNT] = {"vi", "en"};
static const char *const NAME[FD_LANG_COUNT] = {"Ti\u1ebfng Vi\u1ec7t", "English"};
static fd_lang_t s_lang = FD_LANG_VI;

void fd_i18n_init(void)
{
    lv_i18n_init(lv_i18n_language_pack);
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        char v[8] = {0};
        size_t n = sizeof(v);
        if (nvs_get_str(h, "lang", v, &n) == ESP_OK) {
            for (int i = 0; i < FD_LANG_COUNT; i++) {
                if (!strcmp(v, CODE[i])) s_lang = (fd_lang_t)i;
            }
        }
        nvs_close(h);
    }
    lv_i18n_set_locale(CODE[s_lang]);
    ESP_LOGI(TAG, "lang: %s", CODE[s_lang]);
}

fd_lang_t fd_i18n_get_lang(void) { return s_lang; }

void fd_i18n_set_lang(fd_lang_t lang)
{
    if ((unsigned)lang >= FD_LANG_COUNT) return;
    s_lang = lang;
    lv_i18n_set_locale(CODE[lang]);
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "lang", CODE[lang]);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "lang -> %s", CODE[lang]);
}

const char *fd_i18n_lang_name(fd_lang_t lang)
{
    return (unsigned)lang < FD_LANG_COUNT ? NAME[lang] : "?";
}
