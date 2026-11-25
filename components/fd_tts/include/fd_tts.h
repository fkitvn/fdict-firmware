#pragma once
/* TTS tren may: SVOX Pico (Apache-2.0, xem pico/lib/NOTICE). Tai nguyen ngon ngu o phan vung flash voice (mmap)
 * (khong sao chep), vung nho Pico ~1.25 MB trong PSRAM (cap 1 lan). Ngon ngu (en|fr) doc tu header voice.bin.
 * Khong an toan da luong: chi goi tu task fd_audio. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Khoi tao Pico (goi nhieu lan duoc; lan dau that bai thi khong thu lai). */
esp_err_t fd_tts_init(void);
/* Phan vung voice co du lieu giong hop le (header + CRC); false = TTS tat (thieu/hong voice.bin). */
bool fd_tts_voice_ok(void);
/* Ma ngon ngu cua giong dang nap: "en" | "fr" (chuoi tinh, khong free). Mac dinh "en" neu chua map. */
const char *fd_tts_lang(void);
/* true khi giong la tieng Phap. */
bool fd_tts_is_fr(void);
/* Tong hop text (UTF-8, toi da ~60 ky tu) -> PCM 16-bit mono 16 kHz trong *out (malloc PSRAM, nguoi goi free).
 * Tieng Anh: chi ASCII in duoc. Tieng Phap: chap nhan chu Latin co dau (UTF-8). */
esp_err_t fd_tts_synth(const char *text, int16_t **out, size_t *count);
esp_err_t fd_tts_synth_ex(const char *text, int speed_pct, int16_t **out, size_t *count);

/* 5.4.6: tong hop tung buoc, chi goi tu task fd_audio. Bom (pump) trong luc phat manh truoc de khong ngat tieng. */
#define FD_TTS_SRC_MAX 96
typedef struct {
    char src[FD_TTS_SRC_MAX];
    const unsigned char *rp;
    int16_t *buf;
    size_t cap, n, left;
    esp_err_t err;
    uint8_t flags;
} fd_tts_job_t;
void fd_tts_job_init(fd_tts_job_t *job);
esp_err_t fd_tts_job_start(fd_tts_job_t *job, const char *text, int speed_pct);
bool fd_tts_job_pump(fd_tts_job_t *job);
bool fd_tts_job_done(const fd_tts_job_t *job);
esp_err_t fd_tts_job_take(fd_tts_job_t *job, int16_t **out, size_t *count);
void fd_tts_job_abort(fd_tts_job_t *job);

#ifdef __cplusplus
}
#endif
