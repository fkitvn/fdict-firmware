#pragma once
/* Am thanh: ES8311 (I2C chung 0x18) + I2S std 16 kHz, loa qua PA GPIO1 (active-low).
 * Khoi tao luoi o lan phat dau; phat trong task rieng (khong chan task LVGL); PA chi bat khi dang phat. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Am phan hoi tra loi: true = ting (~200 ms), false = buzz tram 2 not (~280 ms). Xep hang doi nhu phat am (khong chan). */
esp_err_t fd_audio_feedback(bool correct);
/* Chuong 2 not ngan (~300 ms), dung khi khong co am tu. */
esp_err_t fd_audio_beep(void);
/* Phat PCM 16-bit mono 16 kHz. Bo dem phai ton tai den khi phat xong (vd du lieu const). */
esp_err_t fd_audio_play_pcm(const int16_t *samples, size_t count);
/* Doc tu tieng Anh bang TTS tren may (Pico) trong task am thanh; loi tong hop -> chuong. text < 40 ky tu. */
esp_err_t fd_audio_say(const char *text);
/* Phat file PCM cache (16-bit mono 16 kHz, doc theo khoi trong task am thanh) neu co; khong co -> Pico doc text roi goi
 * miss_cb(text) (tu task am thanh, phai khong chan). path < 48, text < 40 ky tu. */
esp_err_t fd_audio_say_cached(const char *path, const char *text);
/* Nhu say_cached + tuy chon: FD_SAY_NO_FILE bo qua bo tu/cache (chi Pico), FD_SAY_NO_PICO khong co file -> chuong,
 * FD_SAY_NO_DL khong bao miss (khong tai nen). reps 1..2 lan doc. path co the rong khi co flags. */
#define FD_SAY_NO_FILE 1u
#define FD_SAY_NO_PICO 2u
#define FD_SAY_NO_DL   4u
esp_err_t fd_audio_say_ex(const char *path, const char *text, unsigned flags, int reps);
void fd_audio_set_miss_cb(void (*cb)(const char *text));
/* Tra am trong bo tu offline truoc cache (goi tu task am thanh): true -> phat path[off, off+len). */
void fd_audio_set_lookup_cb(bool (*cb)(const char *word, char *path, size_t plen, uint32_t *off, uint32_t *len));

/* Am luong 0..100 (0 = tat tieng: khong phat, PA tat). Luu NVS fd/vol, mac dinh 70. */
void fd_audio_set_volume(int vol);
int fd_audio_get_volume(void);
/* true khi dang phat (vd: khong cho man hinh ngu). */
bool fd_audio_is_playing(void);
/* true khi dang phat HOAC con yeu cau trong hang doi (vd: tong hop TTS chua xong). */
bool fd_audio_busy(void);
/* Truoc khi tat nguon: tat PA, mute codec, chan phat tiep. */
void fd_audio_power_off(void);
/* Reset codec ES8311 + PA (PA tat ~50 ms, khoi dong lai I2S, reset mem codec, init lai). Lan dau (boot): khoi tao
 * phan cung ngay (reset "boot"); sau do (thuc man hinh): xep yeu cau reset "wake" cho task am thanh (khong chan). */
void fd_audio_reset_codec(void);


#ifdef __cplusplus
}
#endif