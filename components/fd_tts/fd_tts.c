#include "fd_tts.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "picoapi.h"

static const char *TAG = "fd_tts";

#define ARENA_B   (1280 * 1024)
#define TA_NAME   "/pico/en-US_ta.bin"
#define SG_NAME   "/pico/en-US_lh0_sg.bin"
#define VOICE     "FdVoice"
#define TEXT_MAX  60
#define OUT_MAX   (16000 * 6)   /* toi da 6 s */
#define TRIM_TH   300

/* 23/10: du lieu giong o phan vung flash 'voice' (tools/mk_voice.py), mmap - khong sao chep vao RAM */
#define VOICE_PART  "voice"
#define VOICE_MAGIC "FDVOICE1"
typedef struct {
    char magic[8];
    uint32_t version, ta_off, ta_len, sg_off, sg_len, crc32;
} voice_hdr_t;
static const unsigned char *fd_pico_ta_start, *fd_pico_ta_end, *fd_pico_sg_start, *fd_pico_sg_end;
static int s_voice = -1;   /* -1 chua kiem tra, 0 thieu/hong, 1 OK */

/* Map + kiem tra header/CRC phan vung voice (1 lan). */
static bool voice_map(void)
{
    if (s_voice >= 0) return s_voice == 1;
    s_voice = 0;
    const esp_partition_t *pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, VOICE_PART);
    if (!pt) { ESP_LOGE(TAG, "Thieu du lieu giong doc: khong co phan vung '%s' (nap lai bang -full.bin)", VOICE_PART); return false; }
    const void *base = NULL;
    esp_partition_mmap_handle_t h;
    if (esp_partition_mmap(pt, 0, pt->size, ESP_PARTITION_MMAP_DATA, &base, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Thieu du lieu giong doc: mmap '%s' loi", VOICE_PART);
        return false;
    }
    const voice_hdr_t *v = base;
    const unsigned char *b = base;
    uint64_t end_ta = (uint64_t)v->ta_off + v->ta_len, end_sg = (uint64_t)v->sg_off + v->sg_len;
    if (memcmp(v->magic, VOICE_MAGIC, 8) || v->version != 1 || v->ta_off < sizeof(*v) || end_ta > v->sg_off ||
        end_sg > pt->size || !v->ta_len || !v->sg_len) {
        ESP_LOGE(TAG, "Thieu du lieu giong doc: header phan vung '%s' sai (chua nap voice.bin?)", VOICE_PART);
        esp_partition_munmap(h);
        return false;
    }
    uint32_t crc = esp_rom_crc32_le(0, b + sizeof(*v), (uint32_t)(end_sg - sizeof(*v)));
    if (crc != v->crc32) {
        ESP_LOGE(TAG, "Thieu du lieu giong doc: CRC sai (%08lx != %08lx)", (unsigned long)crc, (unsigned long)v->crc32);
        esp_partition_munmap(h);
        return false;
    }
    fd_pico_ta_start = b + v->ta_off;
    fd_pico_ta_end = fd_pico_ta_start + v->ta_len;
    fd_pico_sg_start = b + v->sg_off;
    fd_pico_sg_end = fd_pico_sg_start + v->sg_len;
    s_voice = 1;
    return true;   /* giu mmap suot doi */
}

bool fd_tts_voice_ok(void) { return voice_map(); }

static pico_System s_sys;
static pico_Engine s_eng;
static void *s_mem;
static bool s_tried, s_ok;

/* Goi tu picopal_fopen / picorsrc (da va): ten tai nguyen nhung -> con tro flash. */
const unsigned char *fd_pico_embedded(const char *name, unsigned int *size);
const unsigned char *fd_pico_embedded(const char *name, unsigned int *size)
{
    const unsigned char *a = NULL, *b = NULL;
    if (name && !strcmp(name, TA_NAME)) { a = fd_pico_ta_start; b = fd_pico_ta_end; }
    else if (name && !strcmp(name, SG_NAME)) { a = fd_pico_sg_start; b = fd_pico_sg_end; }
    if (a && size) *size = (unsigned int)(b - a);
    return a;
}

esp_err_t fd_tts_init(void)
{
    if (s_tried) return s_ok ? ESP_OK : ESP_FAIL;
    s_tried = true;
    int64_t t0 = esp_timer_get_time();
    if (!voice_map()) return ESP_ERR_NOT_FOUND;   /* TTS tat, thiet bi van chay */
    s_mem = heap_caps_malloc(ARENA_B, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mem) { ESP_LOGE(TAG, "pico: khong cap duoc %u KB PSRAM", (unsigned)(ARENA_B / 1024)); return ESP_ERR_NO_MEM; }
    pico_Resource ta = NULL, sg = NULL;
    char tan[PICO_RETSTRINGSIZE], sgn[PICO_RETSTRINGSIZE];   /* API ghi toi da PICO_RETSTRINGSIZE */
    pico_Status st = pico_initialize(s_mem, ARENA_B, &s_sys);
    if (!st) st = pico_loadResource(s_sys, (const pico_Char *)TA_NAME, &ta);
    if (!st) st = pico_loadResource(s_sys, (const pico_Char *)SG_NAME, &sg);
    if (!st) st = pico_getResourceName(s_sys, ta, tan);
    if (!st) st = pico_getResourceName(s_sys, sg, sgn);
    if (!st) st = pico_createVoiceDefinition(s_sys, (const pico_Char *)VOICE);
    if (!st) st = pico_addResourceToVoiceDefinition(s_sys, (const pico_Char *)VOICE, (const pico_Char *)tan);
    if (!st) st = pico_addResourceToVoiceDefinition(s_sys, (const pico_Char *)VOICE, (const pico_Char *)sgn);
    if (!st) st = pico_newEngine(s_sys, (const pico_Char *)VOICE, &s_eng);
    if (st) {
        char msg[PICO_RETSTRINGSIZE] = "";
        if (s_sys) pico_getSystemStatusMessage(s_sys, st, msg);
        ESP_LOGE(TAG, "pico init loi %d: %s", (int)st, msg);
        if (s_sys) pico_terminate(&s_sys);
        s_sys = NULL;
        heap_caps_free(s_mem);
        s_mem = NULL;
        return ESP_FAIL;
    }
    s_ok = true;
    ESP_LOGI(TAG, "pico init OK (%d ms): arena %u KB PSRAM, ta %u B + sg %u B (phan vung voice, mmap); PSRAM free %u KB, noi bo free %u KB",
             (int)((esp_timer_get_time() - t0) / 1000), (unsigned)(ARENA_B / 1024),
             (unsigned)(fd_pico_ta_end - fd_pico_ta_start), (unsigned)(fd_pico_sg_end - fd_pico_sg_start),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    return ESP_OK;
}

esp_err_t fd_tts_synth(const char *text, int16_t **out, size_t *count)
{
    if (!text || !out || !count) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *count = 0;
    char in[TEXT_MAX + 2];
    size_t k = 0;
    for (const char *p = text; *p && k < TEXT_MAX; p++)
        if (*p >= 0x20 && *p < 0x7f) in[k++] = *p;
    in[k] = 0;
    if (!k) return ESP_ERR_INVALID_ARG;
    esp_err_t e = fd_tts_init();
    if (e != ESP_OK) return e;

    size_t cap = 16000, n = 0;
    int16_t *buf = heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return ESP_ERR_NO_MEM;
    int16_t tmp[64];
    const pico_Char *p = (const pico_Char *)in;
    pico_Int16 left = (pico_Int16)(k + 1), sent = 0, got = 0, type = 0;   /* +1: ky tu 0 -> flush */
    pico_Status st = PICO_OK;
    while (left > 0 && e == ESP_OK) {
        st = pico_putTextUtf8(s_eng, p, left, &sent);
        if (st) { e = ESP_FAIL; break; }
        left = (pico_Int16)(left - sent);
        p += sent;
        do {
            st = pico_getData(s_eng, tmp, (pico_Int16)sizeof(tmp), &got, &type);
            size_t m = got > 0 ? (size_t)got / 2 : 0;
            if (m && n + m <= OUT_MAX) {
                if (n + m > cap) {
                    size_t nc = cap * 2;
                    int16_t *nb = heap_caps_realloc(buf, nc * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!nb) { e = ESP_ERR_NO_MEM; break; }
                    buf = nb;
                    cap = nc;
                }
                memcpy(buf + n, tmp, m * sizeof(int16_t));
                n += m;
            }
        } while (st == PICO_STEP_BUSY);
        if (e == ESP_OK && st != PICO_STEP_IDLE) e = ESP_FAIL;
    }
    if (e != ESP_OK) {
        char msg[PICO_RETSTRINGSIZE] = "";
        pico_getEngineStatusMessage(s_eng, st, msg);
        ESP_LOGE(TAG, "pico synth loi %d: %s", (int)st, msg);
        pico_resetEngine(s_eng, PICO_RESET_SOFT);
        heap_caps_free(buf);
        return e;
    }
    size_t a = 0, z = n;   /* cat im lang dau/cuoi (giu le ~20/60 ms) */
    while (a < n && abs(buf[a]) < TRIM_TH) a++;
    while (z > a && abs(buf[z - 1]) < TRIM_TH) z--;
    a = a > 320 ? a - 320 : 0;
    z = z + 960 < n ? z + 960 : n;
    if (z <= a) { heap_caps_free(buf); return ESP_FAIL; }
    if (a) memmove(buf, buf + a, (z - a) * sizeof(int16_t));
    *out = buf;
    *count = z - a;
    return ESP_OK;
}