#include "fd_voice.h"
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "fd_audio.h"
#include "fd_net.h"
#include "fd_deck.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_voice";

#define VDIR       "/data/voice"
#define TMP_PATH   VDIR "/.tmp"
#define WORD_MAX   32
#define FILE_MAX   (64 * 1024)
#define CACHE_CAP  (2 * 1024 * 1024)
#define NEG_N      32
#define NEG_404_US (6LL * 3600 * 1000000)
#define NEG_ERR_US (60LL * 1000000)

typedef struct { char w[WORD_MAX]; } job_t;
typedef struct { char w[WORD_MAX]; int64_t until; } neg_t;

static QueueHandle_t s_q;
static SemaphoreHandle_t s_mx;
static neg_t s_neg[NEG_N];
static int s_neg_i;

static bool norm(const char *in, char *out)
{
    size_t k = 0;
    for (; in && in[k]; k++) {
        if (k >= WORD_MAX - 1 || !isalpha((unsigned char)in[k])) return false;
        out[k] = (char)tolower((unsigned char)in[k]);
    }
    out[k] = 0;
    return k > 0;
}

bool fd_voice_path(const char *word, char *out, size_t len)
{
    char w[WORD_MAX];
    if (!norm(word, w)) return false;
    return snprintf(out, len, VDIR "/%s.pcm", w) < (int)len;
}

static bool neg_blocked(const char *w)
{
    bool b = false;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    for (int i = 0; i < NEG_N; i++)
        if (s_neg[i].until > now && !strcmp(s_neg[i].w, w)) { b = true; break; }
    xSemaphoreGive(s_mx);
    return b;
}

static void neg_add(const char *w, int64_t ttl)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    strlcpy(s_neg[s_neg_i].w, w, WORD_MAX);
    s_neg[s_neg_i].until = esp_timer_get_time() + ttl;
    s_neg_i = (s_neg_i + 1) % NEG_N;
    xSemaphoreGive(s_mx);
}

void fd_voice_request(const char *word)
{
    char w[WORD_MAX];
    if (!s_q || !norm(word, w)) return;
    if (!fd_net_is_connected()) { ESP_LOGD(TAG, "tai \"%s\": bo qua (khong WiFi)", w); return; }
    if (!fd_ota_is_paired()) { ESP_LOGD(TAG, "tai \"%s\": bo qua (chua ghep)", w); return; }
    if (neg_blocked(w)) { ESP_LOGD(TAG, "tai \"%s\": bo qua (vua 404/loi)", w); return; }
    job_t j;
    strlcpy(j.w, w, sizeof(j.w));
    if (xQueueSend(s_q, &j, 0) == pdTRUE) ESP_LOGD(TAG, "tai \"%s\": xep hang", w);
    else ESP_LOGI(TAG, "tai \"%s\": hang doi day", w);
}

/* Tong .pcm > CACHE_CAP -> xoa file cu nhat (mtime) den khi vua. */
static void prune(void)
{
    for (int guard = 0; guard < 64; guard++) {
        DIR *d = opendir(VDIR);
        if (!d) return;
        long sum = 0;
        time_t oldest = 0;
        char victim[WORD_MAX + 16] = "";
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n < 5 || n > WORD_MAX + 3 || strcmp(e->d_name + n - 4, ".pcm")) continue;
            char p[sizeof(VDIR) + 1 + 256];   /* d_name toi da 255 */
            snprintf(p, sizeof(p), VDIR "/%s", e->d_name);
            struct stat st;
            if (stat(p, &st) != 0) continue;
            sum += st.st_size;
            if (!victim[0] || st.st_mtime < oldest) { oldest = st.st_mtime; strlcpy(victim, e->d_name, sizeof(victim)); }
        }
        closedir(d);
        if (sum <= CACHE_CAP || !victim[0]) return;
        char p[sizeof(VDIR) + 1 + 256];   /* d_name toi da 255 */
        snprintf(p, sizeof(p), VDIR "/%s", victim);
        unlink(p);
        ESP_LOGI(TAG, "cache %ld B > %d: xoa %s", sum, CACHE_CAP, victim);
    }
}

static void fetch(const char *w)
{
    char dst[WORD_MAX + 24], path[WORD_MAX + 32];
    snprintf(dst, sizeof(dst), VDIR "/%s.pcm", w);
    struct stat st;
    if (stat(dst, &st) == 0) return;   /* da co (yeu cau trung) */
    snprintf(path, sizeof(path), "/api/v1/audio/%s?fmt=pcm16k", w);
    int64_t t0 = esp_timer_get_time();
    int code = fd_ota_api_get_file(path, TMP_PATH, FILE_MAX);
    long sz = (stat(TMP_PATH, &st) == 0) ? (long)st.st_size : 0;
    ESP_LOGI(TAG, "tai \"%s\": HTTP %d, %ld B, %d ms", w, code, sz, (int)((esp_timer_get_time() - t0) / 1000));
    if (code == 200 && sz >= 2 && !(sz & 1)) {
        unlink(dst);
        if (rename(TMP_PATH, dst) == 0) { prune(); return; }
        ESP_LOGI(TAG, "tai \"%s\": rename loi", w);
    }
    unlink(TMP_PATH);
    if (code == 401) return;   /* fd_ota da xoa token */
    neg_add(w, code == 404 ? NEG_404_US : NEG_ERR_US);
}

static void task(void *arg)
{
    (void)arg;
    if (mkdir(VDIR, 0775) != 0) { struct stat st; if (stat(VDIR, &st) != 0) ESP_LOGI(TAG, "mkdir %s loi", VDIR); }
    unlink(TMP_PATH);   /* tai do dang truoc khi mat dien */
    job_t j;
    while (xQueueReceive(s_q, &j, portMAX_DELAY)) fetch(j.w);
}

void fd_voice_init(void)
{
    if (s_q) return;
    s_mx = xSemaphoreCreateMutex();
    QueueHandle_t q = xQueueCreate(8, sizeof(job_t));
    if (!s_mx || !q) { ESP_LOGI(TAG, "init: het bo nho"); return; }
    s_q = q;
    if (xTaskCreate(task, "fd_voice", 8192, NULL, 2, NULL) != pdPASS) ESP_LOGI(TAG, "init: khong tao duoc task");
    fd_audio_set_miss_cb(fd_voice_request);
    fd_deck_init();   /* bo tu offline: meta + dong bo outbox */
    fd_audio_set_lookup_cb(fd_deck_find_audio);   /* thu tu: bo tu -> /data/voice -> Pico */
}

esp_err_t fd_pron_say(const char *text) { return fd_pron_say_cfg(text, NULL); }

esp_err_t fd_pron_say_cfg(const char *text, const fd_pron_cfg_t *cfg)
{
    if (!text) return ESP_ERR_INVALID_ARG;
    while (*text == ' ') text++;
    char t[40];
    strlcpy(t, text, sizeof(t));
    size_t n = strlen(t);
    while (n && t[n - 1] == ' ') t[--n] = 0;
    if (!n) return ESP_ERR_INVALID_ARG;
    /* 23/10: KHONG bo qua khi dang phat (am dung/sai vua xep hang -> tu tu phat / bam loa bi nuot) - xep vao hang doi;
     * hang doi day (bam lien tuc) -> INVALID_STATE (khong bip/toast) */
    fd_voice_init();
    char w[WORD_MAX], path[48];
    esp_err_t r;
    bool word = norm(t, w) && fd_voice_path(w, path, sizeof(path));
    static const fd_pron_cfg_t DEF = {FD_PRON_AUTO, 1, true};
    if (!cfg) cfg = &DEF;
    unsigned fl = cfg->mode == FD_PRON_PICO_ONLY ? FD_SAY_NO_FILE | FD_SAY_NO_DL : cfg->mode == FD_PRON_FILES_ONLY ? FD_SAY_NO_PICO : 0;
    if (!cfg->dl) fl |= FD_SAY_NO_DL;
    if (word) r = fd_audio_say_ex(path, w, fl, cfg->reps);
    else if (cfg->mode == FD_PRON_FILES_ONLY) r = ESP_ERR_NOT_FOUND;   /* cum tu: khong co file */
    else r = fd_audio_say_ex(NULL, t, FD_SAY_NO_FILE | FD_SAY_NO_DL, cfg->reps);   /* cum tu: chi TTS, khong tai */
    if (r == ESP_ERR_TIMEOUT) r = ESP_ERR_INVALID_STATE;
    ESP_LOGD(TAG, "pron \"%s\": mode %u x%u dl %d %s -> %s", t, cfg->mode, cfg->reps, cfg->dl, word ? "tu" : "cum", esp_err_to_name(r));
    return r;
}
