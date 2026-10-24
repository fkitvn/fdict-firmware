#pragma once
/* TTS tieng Anh tren may: SVOX Pico (en-US, Apache-2.0, xem pico/lib/NOTICE). Tai nguyen ngon ngu o phan vung flash voice (mmap)
 * (khong sao chep), vung nho Pico ~1.25 MB trong PSRAM (cap 1 lan). Khong an toan da luong: chi goi tu task fd_audio. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Khoi tao Pico (goi nhieu lan duoc; lan dau that bai thi khong thu lai). */
esp_err_t fd_tts_init(void);
/* Phan vung  voice co du lieu giong hop le (header + CRC); false = TTS tat (thieu/hong voice.bin). */
bool fd_tts_voice_ok(void);
/* Tong hop text (ASCII, toi da ~60 ky tu) -> PCM 16-bit mono 16 kHz trong *out (malloc PSRAM, nguoi goi free). */
esp_err_t fd_tts_synth(const char *text, int16_t **out, size_t *count);

#ifdef __cplusplus
}
#endif