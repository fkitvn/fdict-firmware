#pragma once
/* Giong doc chat luong cao (lai voi Pico): cache /data/voice/<word>.pcm (16-bit LE mono 16 kHz, <= 64 KB).
 * Tai tu GET {base}/api/v1/audio/{word}?fmt=pcm16k (Bearer, xem docs/tts/CONTRACT-firmware.md) trong task "fd_voice"
 * uu tien thap (khong chan LVGL / am thanh). 404 -> nho 6 h; loi mang/5xx -> thu lai sau 60 s; 401 -> fd_ota ve Chua ghep.
 * Cache toi da 2 MB: vuot -> xoa file cu nhat (mtime). */
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tao task + dang ky fd_audio miss_cb (goi nhieu lan duoc). */
void fd_voice_init(void);
/* Duong dan cache cho tu ([a-z], 1..31 ky tu, khong phan biet hoa thuong). false = tu khong hop le. */
bool fd_voice_path(const char *word, char *out, size_t len);
/* Xep tai (khong chan): bo qua neu khong WiFi / chua ghep / vua 404 / hang doi day. */
void fd_voice_request(const char *word);
/* Duong phat am DUNG CHUNG (Flash card, Hoc tap, Phat am). Chi xep hang (goi tu LVGL duoc), tong hop/phat trong task fd_audio.
 * 1 tu [A-Za-z]{1,31} -> cache /data/voice/<tu>.pcm, thieu -> Pico + tai nen; cum tu/ky tu khac -> chi Pico (khong tai).
 * Dang phat / con hang doi -> BO QUA cham moi (ESP_ERR_INVALID_STATE, khong bip). Toi da 39 ky tu.*/
esp_err_t fd_pron_say(const char *text);

/* Tuy chon phat am (man Hoc tap; Flash card / Phat am dung mac dinh qua fd_pron_say). */
typedef enum { FD_PRON_AUTO = 0, FD_PRON_PICO_ONLY, FD_PRON_FILES_ONLY } fd_pron_mode_t;
typedef struct {
    uint8_t mode;   /* fd_pron_mode_t: AUTO = bo tu > cache > Pico; PICO_ONLY; FILES_ONLY = khong Pico (chuong neu thieu) */
    uint8_t reps;   /* 1..2 */
    bool dl;        /* tai giong vao cache khi thieu (co mang) */
} fd_pron_cfg_t;
/* NULL = mac dinh {AUTO, 1, true}. FILES_ONLY + cum tu -> ESP_ERR_NOT_FOUND (khong co file). */
esp_err_t fd_pron_say_cfg(const char *text, const fd_pron_cfg_t *cfg);

#ifdef __cplusplus
}
#endif