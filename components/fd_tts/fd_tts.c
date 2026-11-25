#include "fd_tts.h"
#include <stdbool.h>
#include <stdio.h>
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
/* Ten tai nguyen noi: chi la khoa mmap, khong phai ten that trong lingware. */
#define TA_NAME   "/pico/ta.bin"
#define SG_NAME   "/pico/sg.bin"
#define VOICE     "FdVoice"
#define TEXT_MAX  60
#define OUT_MAX   (16000 * 16)  /* toi da 16 s (du cho 0.5x) */
#define TRIM_TH   300
#define JOB_STARTED 1
#define JOB_DONE    4

/* 23/10: du lieu giong o phan vung flash 'voice' (tools/mk_voice.py), mmap - khong sao chep vao RAM.
 * Byte 32..35 (sau header 32 B): ma ngon ngu ASCII 2 ky tu + \0\0 (vd "en\0\0", "fr\0\0"). */
#define VOICE_PART  "voice"
#define VOICE_MAGIC "FDVOICE1"
typedef struct {
    char magic[8];
    uint32_t version, ta_off, ta_len, sg_off, sg_len, crc32;
} voice_hdr_t;
static const unsigned char *fd_pico_ta_start, *fd_pico_ta_end, *fd_pico_sg_start, *fd_pico_sg_end;
static int s_voice = -1;   /* -1 chua kiem tra, 0 thieu/hong, 1 OK */
static char s_lang[4] = "en";   /* "en" | "fr" */

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
    /* Ma ngon ngu o byte 32..35 (truoc ta_off=36). Du lieu cu toan 0 -> mac dinh en. */
    s_lang[0] = 'e';
    s_lang[1] = 'n';
    s_lang[2] = 0;
    if (v->ta_off >= sizeof(*v) + 2) {
        const unsigned char *lang = b + sizeof(*v);
        if (lang[0] == 'f' && lang[1] == 'r') {
            s_lang[0] = 'f';
            s_lang[1] = 'r';
        } else if (lang[0] == 'e' && lang[1] == 'n') {
            s_lang[0] = 'e';
            s_lang[1] = 'n';
        }
    }
    s_voice = 1;
    ESP_LOGI(TAG, "voice ok: lang=%s ta=%u sg=%u", s_lang, (unsigned)v->ta_len, (unsigned)v->sg_len);
    return true;   /* giu mmap suot doi */
}

bool fd_tts_voice_ok(void) { return voice_map(); }

const char *fd_tts_lang(void)
{
    (void)voice_map();
    return s_lang;
}

bool fd_tts_is_fr(void)
{
    return fd_tts_lang()[0] == 'f';
}

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


void fd_tts_job_init(fd_tts_job_t *job)
{
    if (job) memset(job, 0, sizeof(*job));
}

void fd_tts_job_abort(fd_tts_job_t *job)
{
    if (!job) return;
    if ((job->flags & JOB_STARTED) && !(job->flags & JOB_DONE) && s_ok && s_eng) pico_resetEngine(s_eng, PICO_RESET_SOFT);
    if (job->buf) heap_caps_free(job->buf);
    memset(job, 0, sizeof(*job));
}

esp_err_t fd_tts_job_start(fd_tts_job_t *job, const char *text, int speed_pct)
{
    if (!job) return ESP_ERR_INVALID_ARG;
    fd_tts_job_abort(job);
    if (!text) return ESP_ERR_INVALID_ARG;
    if (speed_pct < 50) speed_pct = 50;
    if (speed_pct > 200) speed_pct = 200;
    char in[TEXT_MAX + 2];
    size_t k = 0;
    const bool fr = fd_tts_is_fr();
    for (const char *p = text; *p && k < TEXT_MAX; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '<' || c == '>') in[k++] = ' ';
        else if (c >= 0x20 && c < 0x7f) in[k++] = (char)c;
        else if (fr && c >= 0x80) in[k++] = (char)c;
    }
    in[k] = 0;
    if (!k) return ESP_ERR_INVALID_ARG;
    esp_err_t e = fd_tts_init();
    if (e != ESP_OK) { job->err = e; return e; }
    if (speed_pct != 100) {
        int m = snprintf(job->src, sizeof(job->src), "<speed level=\"%d\">%s</speed>", speed_pct, in);
        if (m > 0 && (size_t)m < sizeof(job->src)) k = (size_t)m;
        else memcpy(job->src, in, k + 1);
    } else {
        memcpy(job->src, in, k + 1);
    }
    job->rp = (const unsigned char *)job->src;
    job->left = k + 1;   /* +1: ky tu 0 -> flush */
    job->cap = 16000;
    job->n = 0;
    job->buf = heap_caps_malloc(job->cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!job->buf) { job->err = ESP_ERR_NO_MEM; return job->err; }
    job->err = ESP_OK;
    job->flags = JOB_STARTED;
    return ESP_OK;
}

bool fd_tts_job_done(const fd_tts_job_t *job)
{
    return !job || !(job->flags & JOB_STARTED) || (job->flags & JOB_DONE);
}

bool fd_tts_job_pump(fd_tts_job_t *job)
{
    if (!job || !(job->flags & JOB_STARTED) || (job->flags & JOB_DONE)) return true;
    if (job->err != ESP_OK) { job->flags |= JOB_DONE; return true; }
    pico_Int16 sent = 0, got = 0, type = 0;
    pico_Status st = PICO_OK;
    if (job->left > 0) {
        st = pico_putTextUtf8(s_eng, (const pico_Char *)job->rp, (pico_Int16)job->left, &sent);
        if (st) {
            char msg[PICO_RETSTRINGSIZE] = "";
            pico_getEngineStatusMessage(s_eng, st, msg);
            ESP_LOGE(TAG, "pico synth loi %d: %s", (int)st, msg);
            pico_resetEngine(s_eng, PICO_RESET_SOFT);
            job->err = ESP_FAIL;
            job->flags |= JOB_DONE;
            return true;
        }
        job->left = (size_t)((pico_Int16)job->left - sent);
        job->rp += sent;
        return false;
    }
    int16_t tmp[64];
    int steps = 12;
    do {
        st = pico_getData(s_eng, tmp, (pico_Int16)sizeof(tmp), &got, &type);
        size_t m = got > 0 ? (size_t)got / 2 : 0;
        if (m && job->n + m <= OUT_MAX) {
            if (job->n + m > job->cap) {
                size_t nc = job->cap * 2;
                if (nc > OUT_MAX) nc = OUT_MAX;
                if (nc < job->n + m) nc = job->n + m;
                int16_t *nb = heap_caps_realloc(job->buf, nc * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!nb) {
                    pico_resetEngine(s_eng, PICO_RESET_SOFT);
                    job->err = ESP_ERR_NO_MEM;
                    job->flags |= JOB_DONE;
                    return true;
                }
                job->buf = nb;
                job->cap = nc;
            }
            memcpy(job->buf + job->n, tmp, m * sizeof(int16_t));
            job->n += m;
        } else if (m && job->n + m > OUT_MAX) {
            if (!(job->flags & 8)) {
                ESP_LOGW(TAG, "tts cat vi qua %d s", (int)(OUT_MAX / 16000));
                job->flags |= 8;
            }
        }
        if (st == PICO_STEP_IDLE) {
            job->flags |= JOB_DONE;
            return true;
        }
        if (st != PICO_STEP_BUSY) {
            char msg[PICO_RETSTRINGSIZE] = "";
            pico_getEngineStatusMessage(s_eng, st, msg);
            ESP_LOGE(TAG, "pico synth loi %d: %s", (int)st, msg);
            pico_resetEngine(s_eng, PICO_RESET_SOFT);
            job->err = ESP_FAIL;
            job->flags |= JOB_DONE;
            return true;
        }
    } while (--steps > 0);
    return false;
}

esp_err_t fd_tts_job_take(fd_tts_job_t *job, int16_t **out, size_t *count)
{
    if (!job || !out || !count) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *count = 0;
    if (!(job->flags & JOB_DONE)) return ESP_ERR_INVALID_STATE;
    if (job->err != ESP_OK) {
        esp_err_t e = job->err;
        fd_tts_job_abort(job);
        return e;
    }
    int16_t *buf = job->buf;
    size_t n = job->n;
    job->buf = NULL;
    job->flags = 0;
    job->n = job->cap = job->left = 0;
    if (!buf || !n) { if (buf) heap_caps_free(buf); return ESP_FAIL; }
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

esp_err_t fd_tts_synth_ex(const char *text, int speed_pct, int16_t **out, size_t *count)
{
    fd_tts_job_t job;
    fd_tts_job_init(&job);
    esp_err_t e = fd_tts_job_start(&job, text, speed_pct);
    if (e != ESP_OK) return e;
    while (!fd_tts_job_pump(&job)) {}
    return fd_tts_job_take(&job, out, count);
}

esp_err_t fd_tts_synth(const char *text, int16_t **out, size_t *count)
{
    return fd_tts_synth_ex(text, 100, out, count);
}