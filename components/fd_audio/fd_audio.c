#include "fd_audio.h"
#include <math.h>
#include <string.h>
#include "board.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fd_es8311.h"
#include "fd_tts.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "fd_audio";

#define SAMPLE_RATE 16000
#define VOL_DEFAULT 100   /* 4.1.3: mac dinh khi chua luu NVS */
#define CHUNK       256   /* mau mono / lan ghi */
#define PA_STARTUP_MS 60   /* truoc: 10 ms */
#define DRAIN_CHUNKS  8    /* 8 x 256 mau = 128 ms im lang > do sau DMA mac dinh (6 x 240 khung = 90 ms) */
#define PA_TAIL_MS    40   /* truoc: 30 ms sau 1 khoi 0 -> cat mat ~60-90 ms cuoi */
#define PA_HOLD_OFF_MS 50  /* reset: giu PA tat truoc khi reset codec */
#define REQ_RESET     SIZE_MAX   /* req_t.count: yeu cau reset codec+PA (thuc) */
#define REQ_REC       (SIZE_MAX - 2)   /* 2.1.8: ghi am thu tu mic (I2S RX) */
#define REQ_WAV       (SIZE_MAX - 3)   /* 2.1.8: phat file WAV ghi am (req_t.path) */
#define REQ_LONG      (SIZE_MAX - 4)   /* doan Nhai: pcm tro toi chuoi PSRAM, task tu giai phong */
#define WAV_HDR       44

/* ===== 4.6.10: CHUAN HOA AM LUONG CHUNG (moi am ra loa di qua write_mono -> lvl_sample) =====
 * Muc chung FD_AUDIO_TARGET_RMS_DBFS / FD_AUDIO_PEAK_CEIL_DBFS (fd_audio.h), chuan theo giong doc Pico (do tren host:
 * RMS doan co tieng ~ -17 dBFS, dinh trung vi -4.4 dBFS; 4.7.1 muc chung -12 dBFS). Am luong nguoi dung + tran
 * codec 0 dB ap dung SAU buoc nay.
 * - Biet ca doan (PCM, TTS, file/doan <= LVL_BUF_MAX, WAV ghi am): 1 gain tinh tu RMS cac khung 20 ms co tieng
 *   (khung < -50 dBFS bo qua -> khong keo nhieu nen len), kep [LVL_GAIN_MIN, gmax]. Khong bom/nhay am trong 1 doan.
 * - File dai hon LVL_BUF_MAX (doc theo khoi): AGC theo bao RMS (attack 10 ms / release 400 ms), gain giam 5 ms /
 *   tang 300 ms, chi cap nhat khi tren nguong im lang, kep gmax.
 * - Am tong hop (chuong, phan hoi dung/sai, bip am luong, am thu loa) cung chuan hoa ca doan nhu tren.
 * Sau gain luon qua bo gioi han mem (tanh tu 70% tran) -> khong bao gio cat cung, dinh <= -1 dBFS. */
#define LVL_FRAME        320                 /* 20 ms */
#define LVL_GATE_DBFS    (-50.0f)            /* khung/bao duoi muc nay = im lang */
#define LVL_GAIN_MIN     0.125f              /* -18 dB */
#define LVL_GAIN_MAX     4.0f                /* +12 dB: TTS, file am tu, PCM */
#define LVL_GAIN_MAX_MIC 20.0f               /* +26 dB: ghi am mic tho */
#define LVL_KNEE         0.7f                /* bo gioi han bat dau tu 70% tran */
#define LVL_BUF_MAX      (256 * 1024)        /* <= 8 s: nap PSRAM, chuan hoa ca doan; dai hon: AGC */
#define AGC_ATTACK_MS    10
#define AGC_RELEASE_MS   400
#define AGC_GAIN_DOWN_MS 5
#define AGC_GAIN_UP_MS   300

typedef struct {
    const int16_t *pcm;   /* NULL = chuong */
    size_t count;
    char text[40];        /* khac rong = doc tu (TTS Pico) roi phat */
    char path[48];        /* khac rong: thu file PCM cache truoc (giong tot), thieu -> Pico text + bao miss */
    uint8_t flags;        /* FD_SAY_*: khac 0 -> di say_word du path rong */
    uint8_t reps;         /* so lan doc (1..2) */
} req_t;

static QueueHandle_t s_q;
static i2s_chan_handle_t s_tx, s_rx;   /* 2.1.8 s_rx: ADC ES8311 (mic analog) qua DIN GPIO6, song cong voi TX */
static bool s_hw_ok;
static fd_es8311_handle_t s_es;
static i2c_master_dev_handle_t s_dev;   /* ES8311 0x18 */
static volatile int s_vol = -1;   /* -1 = chua doc NVS */
static int s_vol_applied = -1;
static volatile bool s_playing;
static volatile bool s_say_stop;
static volatile bool s_rec_on;   /* 2.1.8 dang ghi am (hoac da xep yeu cau ghi) */

/* Trang thai chuan hoa (chi task am thanh dung; lvl_init goi truoc khi tong hop/phat). */
static float s_lvl_tgt, s_lvl_ceil, s_lvl_knee, s_lvl_gate;
static float s_gain = 1.0f, s_agc_gmax, s_agc_env;
static bool s_agc;
static float s_agc_att, s_agc_rel, s_agc_down, s_agc_up;

static float coef_ms(int ms) { return 1.0f - expf(-1000.0f / ((float)ms * SAMPLE_RATE)); }

static void lvl_init(void)
{
    if (s_lvl_tgt > 0.0f) return;
    s_lvl_tgt = 32768.0f * powf(10.0f, FD_AUDIO_TARGET_RMS_DBFS / 20.0f);
    s_lvl_ceil = 32767.0f * powf(10.0f, FD_AUDIO_PEAK_CEIL_DBFS / 20.0f);
    s_lvl_knee = LVL_KNEE * s_lvl_ceil;
    s_lvl_gate = 32768.0f * powf(10.0f, LVL_GATE_DBFS / 20.0f);
    s_agc_att = coef_ms(AGC_ATTACK_MS);
    s_agc_rel = coef_ms(AGC_RELEASE_MS);
    s_agc_down = coef_ms(AGC_GAIN_DOWN_MS);
    s_agc_up = coef_ms(AGC_GAIN_UP_MS);
}

/* Bien do dinh song sin co RMS = muc chung (am tong hop sinh gan muc chung, gain chuan hoa ~1). */
static float lvl_tone_amp(void) { lvl_init(); return s_lvl_tgt * (float)M_SQRT2; }

/* RMS cac khung 20 ms co tieng (bo khung im lang); 0 = toan im lang. */
static float active_rms(const int16_t *p, size_t n)
{
    double sum = 0.0;
    size_t used = 0;
    const double gate2 = (double)s_lvl_gate * s_lvl_gate;
    for (size_t f = 0; f < n; f += LVL_FRAME) {
        size_t m = n - f < LVL_FRAME ? n - f : LVL_FRAME;
        double e = 0.0;
        for (size_t i = 0; i < m; i++) e += (double)p[f + i] * p[f + i];
        if (e / (double)m > gate2) { sum += e; used += m; }
    }
    return used ? (float)sqrt(sum / (double)used) : 0.0f;
}

static void lvl_fixed(float g) { s_agc = false; s_gain = g; }

static void lvl_buffer(const int16_t *p, size_t n, float gmax)
{
    lvl_init();
    float r = active_rms(p, n), g = r > 0.0f ? s_lvl_tgt / r : 1.0f;
    g = g < LVL_GAIN_MIN ? LVL_GAIN_MIN : g > gmax ? gmax : g;
    lvl_fixed(g);
    ESP_LOGD(TAG, "lvl: rms %.1f dBFS -> gain %.2f (%+.1f dB)", r > 0.0f ? (double)(20.0f * log10f(r / 32768.0f)) : -99.0,
             (double)g, (double)(20.0f * log10f(g)));
}

static void lvl_agc(float gmax)
{
    lvl_init();
    s_agc = true;
    s_agc_gmax = gmax;
    s_agc_env = s_lvl_tgt * s_lvl_tgt;
    s_gain = 1.0f;
}

/* Gain (co dinh hoac AGC) + bo gioi han mem: |y| <= tran, muot (khong cat cung). */
static int16_t lvl_sample(int16_t in)
{
    float x = (float)in;
    if (s_agc) {
        float x2 = x * x;
        s_agc_env += (x2 > s_agc_env ? s_agc_att : s_agc_rel) * (x2 - s_agc_env);
        if (s_agc_env > s_lvl_gate * s_lvl_gate) {
            float gt = s_lvl_tgt / sqrtf(s_agc_env);
            gt = gt < LVL_GAIN_MIN ? LVL_GAIN_MIN : gt > s_agc_gmax ? s_agc_gmax : gt;
            s_gain += (gt < s_gain ? s_agc_down : s_agc_up) * (gt - s_gain);
        }
    }
    float y = x * s_gain, a = fabsf(y);
    if (a > s_lvl_knee) {
        float r = s_lvl_ceil - s_lvl_knee;
        a = s_lvl_knee + r * tanhf((a - s_lvl_knee) / r);
        y = y < 0.0f ? -a : a;
    }
    return (int16_t)lrintf(y);
}
bool fd_audio_is_playing(void) { return s_playing; }
bool fd_audio_busy(void) { return s_playing || s_rec_on || (s_q && uxQueueMessagesWaiting(s_q) > 0); }

int fd_audio_get_volume(void)
{
    if (s_vol < 0) {
        int v = VOL_DEFAULT;
        nvs_handle_t h;
        if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
            uint8_t b;
            if (nvs_get_u8(h, "vol", &b) == ESP_OK && b <= 100) v = b;
            nvs_close(h);
        }
        s_vol = v;
    }
    return s_vol;
}

void fd_audio_set_volume(int vol)
{
    vol = vol < 0 ? 0 : vol > 100 ? 100 : vol;
    if (vol == fd_audio_get_volume()) return;
    s_vol = vol;   /* task phat ap dung vao codec truoc lan phat ke tiep */
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "vol", (uint8_t)vol);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void amp(bool on)   /* muc bat = BOARD_AMP_ON_LEVEL */
{
    esp_err_t e = gpio_set_level(BOARD_AMP_EN, on ? BOARD_AMP_ON_LEVEL : !BOARD_AMP_ON_LEVEL);
    ESP_LOGD(TAG, "PA %s: set %s, GPIO%d level=%d", on ? "on" : "off", esp_err_to_name(e), (int)BOARD_AMP_EN,
             gpio_get_level(BOARD_AMP_EN));
}

/* Reset codec+PA (boot va moi lan thuc man hinh): PA va ES8311 van co nguon pin khi ESP32 reset/nap fw -> co the ket
 * (chi rut pin moi het). Board khong co GPIO cap/cat nguon audio/PA -> chi reset bang PA EN + I2C + I2S.
 * 1) PA tat ~50 ms; 2) khoi dong lai kenh I2S (disable -> enable) cho MCLK sach; 3) ES8311 tat DAC/analog + reset mem;
 * 4) codec_init binh thuong. */
static esp_err_t codec_reset(const char *why)
{
    ESP_LOGI(TAG, "reset codec+PA (%s)", why);
    amp(false);
    vTaskDelay(pdMS_TO_TICKS(PA_HOLD_OFF_MS));
    static bool s_en;   /* boot: kenh chua enable -> khong goi disable (tranh log E i2s_common) */
    esp_err_t e = s_en ? i2s_channel_disable(s_tx) : ESP_OK;   /* boot: kenh chua enable -> INVALID_STATE, binh thuong */
    ESP_LOGD(TAG, "i2s disable: %s", esp_err_to_name(e));
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s en");
    s_en = true;   /* MCLK chay truoc khi cau hinh codec */
    vTaskDelay(pdMS_TO_TICKS(5));
    e = fd_es8311_hard_reset(s_dev);
    ESP_LOGD(TAG, "es8311 hard reset: %s", esp_err_to_name(e));
    ESP_RETURN_ON_ERROR(e, TAG, "es8311 reset");
    if (s_es) { fd_es8311_delete(s_es); s_es = NULL; }
    esp_err_t ce = fd_es8311_codec_init(s_dev, SAMPLE_RATE, fd_audio_get_volume(), &s_es);
    ESP_LOGD(TAG, "es8311 codec_init(vol=%d): %s", fd_audio_get_volume(), esp_err_to_name(ce));
    ESP_RETURN_ON_ERROR(ce, TAG, "es8311");
    /* 1.0.2: ES8311 co nguon pin rieng -> thanh ghi mute DAC (0x31 bit5/6) do fd_audio_power_off (truoc deep sleep)
     * dat SONG SOT qua reset mem 0x00=0x1F va reboot ESP32; codec_init khong ghi 0x31 -> sau khi thuc DAC van mute
     * -> loa im toi khi rut pin. Luon bo mute sau init. */
    esp_err_t me = fd_es8311_voice_mute(s_es, false);
    ESP_LOGD(TAG, "es8311 unmute: %s", esp_err_to_name(me));
    ESP_RETURN_ON_ERROR(me, TAG, "es8311 unmute");
    s_vol_applied = fd_audio_get_volume();
    ESP_LOGD(TAG, "am luong %d", s_vol_applied);
    return ESP_OK;
}

static esp_err_t hw_init(void)
{
    gpio_hold_dis(BOARD_AMP_EN);   /* 1.0.2: power_off giu PA tat qua deep sleep -> nha de dieu khien lai */
    gpio_set_level(BOARD_AMP_EN, !BOARD_AMP_ON_LEVEL);   /* muc tat dat truoc khi bat output (khong nhay PA) */
    gpio_config_t g = {.pin_bit_mask = 1ULL << BOARD_AMP_EN, .mode = GPIO_MODE_INPUT_OUTPUT};   /* doc lai muc PA */
    ESP_RETURN_ON_ERROR(gpio_config(&g), TAG, "pa gpio");
    amp(false);
    vTaskDelay(pdMS_TO_TICKS(PA_HOLD_OFF_MS));

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, &s_rx), TAG, "i2s chan");
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = BOARD_I2S_MCLK, .bclk = BOARD_I2S_BCLK, .ws = BOARD_I2S_WS,
                     .dout = BOARD_I2S_DOUT, .din = BOARD_I2S_DIN},
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &sc), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &sc), TAG, "i2s rx");   /* chi enable khi ghi am */

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c");
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x18, .scl_speed_hz = 400000};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_bus(), &dc, &s_dev), TAG, "i2c dev");
    ESP_RETURN_ON_ERROR(codec_reset("boot"), TAG, "reset");
    ESP_LOGI(TAG, "san sang: ES8311 + I2S %d Hz, PA GPIO%d", SAMPLE_RATE, (int)BOARD_AMP_EN);
    return ESP_OK;
}

static bool s_quiet;   /* phat file theo khoi: khong log moi lan ghi */
static void (*s_miss_cb)(const char *text);
static bool (*s_lookup_cb)(const char *word, char *path, size_t plen, uint32_t *off, uint32_t *len);

static void write_mono(const int16_t *m, size_t n)
{
    int16_t st[CHUNK * 2];
    size_t total = 0, req = n * 4;
    esp_err_t last = ESP_OK;
    int64_t t0 = esp_timer_get_time();
    while (n) {
        size_t k = n > CHUNK ? CHUNK : n;
        for (size_t i = 0; i < k; i++) st[2 * i] = st[2 * i + 1] = lvl_sample(m[i]);   /* chuan hoa chung */
        size_t w = 0;
        esp_err_t e = i2s_channel_write(s_tx, st, k * 4, &w, 1000);
        total += w;
        if (e != ESP_OK) { last = e; ESP_LOGD(TAG, "i2s write %s (%u/%u B)", esp_err_to_name(e), (unsigned)w, (unsigned)(k * 4)); }
        m += k;
        n -= k;
    }
    if (!s_quiet) ESP_LOGD(TAG, "i2s write %s: %u/%u B, %d ms", esp_err_to_name(last), (unsigned)total, (unsigned)req,
             (int)((esp_timer_get_time() - t0) / 1000));
}

/* 2 not (E6 ~1319 Hz roi A5 ~880 Hz... dao lai cho de chiu: A5 -> E6), moi not 150 ms, bao duong mem.
 * 4.6.10: tong hop 1 lan vao PSRAM roi chuan hoa ca doan nhu moi nguon khac (bao giam dan -> khong dung gain 1). */
#define CHIME_PER (SAMPLE_RATE * 150 / 1000)
static void play_chime(void)
{
    static const float F[2] = {880.0f, 1318.5f};
    static int16_t *s_chime;
    if (!s_chime) {
        int16_t *b = heap_caps_malloc(2 * CHIME_PER * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!b) return;
        const float a0 = lvl_tone_amp();
        for (int t = 0; t < 2; t++) {
            for (int n = 0; n < CHIME_PER; n++) {
                float env = n < 160 ? n / 160.0f : expf(-3.0f * (float)(n - 160) / CHIME_PER);
                b[t * CHIME_PER + n] = (int16_t)(a0 * env * sinf(2.0f * (float)M_PI * F[t] * n / SAMPLE_RATE));
            }
        }
        s_chime = b;
    }
    lvl_buffer(s_chime, 2 * CHIME_PER, LVL_GAIN_MAX);
    write_mono(s_chime, 2 * CHIME_PER);
}

/* TTS (Pico) ngay trong task am thanh (khong chan LVGL); loi -> chuong. Bo dem PCM o PSRAM (malloc lon). */
static void say_text(const char *text)
{
    int16_t *pcm = NULL;
    size_t n = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t e = fd_tts_synth(text, &pcm, &n);
    ESP_LOGD(TAG, "tts \"%s\": %s, %u mau (%u ms am), tong hop %d ms, stack con %u B", text, esp_err_to_name(e), (unsigned)n,
             (unsigned)(n * 1000 / SAMPLE_RATE), (int)((esp_timer_get_time() - t0) / 1000), (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (e == ESP_OK && n) { lvl_buffer(pcm, n, LVL_GAIN_MAX); write_mono(pcm, n); }
    else play_chime();
    free(pcm);
}

/* Pico chi nhan khoang 60 byte moi lan. Cat theo khoang trang, khong cat giua byte UTF-8. */
static void play_long(char *text)
{
    s_say_stop = false;
    static const int16_t gap[800] = {0};   /* ~50 ms giua cac manh */
    const char *p = text ? text : "";
    bool any = false;
    while (*p && !s_say_stop) {
        while (*p == ' ') p++;
        if (!*p) break;
        int n = 0;
        int end = 0;
        int brk = 0;
        while (p[n] && n < 56) {
            unsigned char c = (unsigned char)p[n];
            int w = 1;
            if ((c & 0xE0) == 0xC0) w = 2;
            else if ((c & 0xF0) == 0xE0) w = 3;
            else if ((c & 0xF8) == 0xF0) w = 4;
            if (n + w > 56) break;
            n += w;
            end = n;
            if (c < 0x80) brk = n;
        }
        if (p[end] && brk > 0) end = brk;
        while (end > 0 && p[end - 1] == ' ') end--;
        if (end <= 0) break;
        char chunk[64];
        memcpy(chunk, p, (size_t)end);
        chunk[end] = 0;
        if (any) write_mono(gap, 800);
        say_text(chunk);
        any = true;
        p += end;
    }
}

static void free_long(const req_t *r)
{
    if (r && r->count == REQ_LONG && r->pcm) free((void *)r->pcm);
}

/* Phat n mau PCM tu vi tri hien tai cua f. <= LVL_BUF_MAX: nap PSRAM, chuan hoa ca doan; dai hon / het PSRAM:
 * doc theo khoi 512 mau qua AGC. Tra ve so mau da phat. */
static size_t play_file(FILE *f, size_t n, float gmax)
{
    static int16_t fb[512];
    size_t tot = 0, k;
    int16_t *b = n * 2 <= LVL_BUF_MAX ? heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    s_quiet = true;
    if (b) {
        tot = fread(b, 2, n, f);
        lvl_buffer(b, tot, gmax);
        write_mono(b, tot);
        heap_caps_free(b);
    } else {
        lvl_agc(gmax);
        while (n && (k = fread(fb, 2, n < sizeof(fb) / 2 ? n : sizeof(fb) / 2, f)) > 0) { write_mono(fb, k); tot += k; n -= k; }
    }
    s_quiet = false;
    return tot;
}

/* So byte tu off toi het file (0 = loi / rong); de f o vi tri off. */
static uint32_t file_left(FILE *f, uint32_t off)
{
    if (fseek(f, 0, SEEK_END) != 0) return 0;
    long e = ftell(f);
    if (e <= (long)off || fseek(f, (long)off, SEEK_SET) != 0) return 0;
    return (uint32_t)(e - (long)off);
}

/* Phat PCM tu file: [off, off+len) (len 0 = het file). false = khong mo duoc / rong. */
static bool play_cached(const char *path, uint32_t off, uint32_t len, const char *what)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t left = file_left(f, off);
    if (len && len < left) left = len;
    int64_t t0 = esp_timer_get_time();
    size_t tot = left ? play_file(f, left / 2, LVL_GAIN_MAX) : 0;
    fclose(f);
    ESP_LOGD(TAG, "%s hit %s@%u: %u mau, %d ms", what, path, (unsigned)off, (unsigned)tot, (int)((esp_timer_get_time() - t0) / 1000));
    return tot > 0;
}

/* Thu tu: am trong bo tu offline (lookup_cb) -> cache /data/voice -> Pico (+ miss_cb tai nen). FD_SAY_* bo bot nguon;
 * khong co file va FD_SAY_NO_PICO -> chuong (bao thieu am). */
static void say_word(const req_t *r)
{
    if (!(r->flags & FD_SAY_NO_FILE)) {
        char dp[96];
        uint32_t o = 0, l = 0;
        if (s_lookup_cb && s_lookup_cb(r->text, dp, sizeof(dp), &o, &l) && play_cached(dp, o, l, "deck")) return;
        if (r->path[0] && play_cached(r->path, 0, 0, "voice cache")) return;
    }
    if (r->flags & FD_SAY_NO_PICO) { ESP_LOGI(TAG, "khong co file am \"%s\" (khong Pico) -> chuong", r->text); play_chime(); return; }
    ESP_LOGD(TAG, "voice %s -> Pico", r->flags & FD_SAY_NO_FILE ? "chi Pico" : "cache miss");
    say_text(r->text);
    if (!(r->flags & FD_SAY_NO_DL) && r->path[0] && s_miss_cb) s_miss_cb(r->text);   /* vd: xep tai giong tot cho lan sau */
}

/* 2.1.8 Ghi am thu: mic analog ES8311 -> ADC -> I2S RX (16 kHz, khe trai/phai: chon khe co nang luong lon hon).
 * Ghi vao bo dem PSRAM (khong ghi flash trong luc doc DMA -> khong tran), het gio/dung -> luu WAV 16-bit mono.
 * PA tat suot luc ghi (khong hu loa / hoi tiep). */
static volatile bool s_rec_stop;
static volatile uint32_t s_rec_n;
static volatile int s_rec_lvl;
static volatile esp_err_t s_rec_err = ESP_ERR_NOT_FOUND;
static char s_rec_path[48];
static uint32_t s_rec_max;   /* so mau toi da */

static void wav_hdr(uint8_t *h, uint32_t data_bytes)
{
    const uint32_t riff = 36 + data_bytes, fmt_len = 16, sr = SAMPLE_RATE, br = SAMPLE_RATE * 2;
    const uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    memcpy(h, "RIFF", 4); memcpy(h + 4, &riff, 4); memcpy(h + 8, "WAVEfmt ", 8); memcpy(h + 16, &fmt_len, 4);
    memcpy(h + 20, &pcm, 2); memcpy(h + 22, &ch, 2); memcpy(h + 24, &sr, 4); memcpy(h + 28, &br, 4);
    memcpy(h + 32, &ba, 2); memcpy(h + 34, &bits, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &data_bytes, 4);
}

static int lvl_of(int peak)   /* dinh -> 0..100 (thang dB -60..0) */
{
    if (peak < 33) return 0;
    int l = (int)((20.0f * log10f((float)peak / 32767.0f) + 60.0f) * 100.0f / 60.0f);
    return l < 0 ? 0 : l > 100 ? 100 : l;
}

static esp_err_t rec_capture(int16_t *pcm, uint32_t *out_n)
{
    fd_es8311_microphone_config(s_es, false);   /* mic analog + PGA, ADC gain 0x17 */
    fd_es8311_microphone_gain_set(s_es, ES8311_MIC_GAIN_30DB);   /* 2.2.9: 24 -> 30 dB */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "i2s rx en");
    int16_t st[CHUNK * 2];
    uint32_t n = 0, skip = SAMPLE_RATE / 10;   /* bo ~100 ms dau (tieng bup khi bat ADC) */
    int fails = 0;
    esp_err_t err = ESP_OK;
    while (!s_rec_stop && n < s_rec_max) {
        size_t r = 0;
        esp_err_t e = i2s_channel_read(s_rx, st, sizeof(st), &r, 200);
        if (e != ESP_OK || r < 4) {
            if (++fails > 10) { err = e != ESP_OK ? e : ESP_ERR_TIMEOUT; break; }
            continue;
        }
        fails = 0;
        size_t k = r / 4;
        int32_t el = 0, er = 0;
        for (size_t i = 0; i < k; i++) { el += abs(st[2 * i]); er += abs(st[2 * i + 1]); }
        int off = er > el ? 1 : 0, peak = 0;
        for (size_t i = 0; i < k && n < s_rec_max; i++) {
            if (skip) { skip--; continue; }
            int16_t v = st[2 * i + off];
            int a = abs(v);
            if (a > peak) peak = a;
            pcm[n++] = v;
        }
        s_rec_n = n;
        s_rec_lvl = lvl_of(peak);
    }
    i2s_channel_disable(s_rx);
    *out_n = n;
    return err;
}

static void rec_run(void)
{
    esp_err_t err = ESP_OK;
    int16_t *pcm = NULL;
    uint32_t n = 0;
    if (!s_hw_ok || !s_rx || !s_es) err = ESP_ERR_INVALID_STATE;
    else if (!(pcm = heap_caps_malloc(s_rec_max * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) err = ESP_ERR_NO_MEM;
    else err = rec_capture(pcm, &n);
    if (err == ESP_OK && n < SAMPLE_RATE / 10) err = ESP_ERR_INVALID_SIZE;   /* qua ngan (< 0.1 s) */
    if (err == ESP_OK) {
        /* 4.6.10: luu ban tho; khi nghe lai play_wav chuan hoa theo muc chung (thay quy tac dinh -1 dBFS cu). */
        int32_t pk = 0;
        for (uint32_t i = 0; i < n; i++) { int32_t a = abs((int32_t)pcm[i]); if (a > pk) pk = a; }
        ESP_LOGI(TAG, "ghi am: dinh %d", (int)pk);
        uint8_t h[WAV_HDR];
        wav_hdr(h, n * 2);
        FILE *f = fopen(s_rec_path, "wb");
        if (!f) err = ESP_FAIL;
        else {
            bool ok = fwrite(h, 1, WAV_HDR, f) == WAV_HDR && fwrite(pcm, 2, n, f) == n;
            if (fclose(f) != 0) ok = false;
            if (!ok) { remove(s_rec_path); err = ESP_ERR_NO_MEM; }
        }
    }
    free(pcm);
    ESP_LOGI(TAG, "ghi am: %u mau (%u ms) -> %s: %s", (unsigned)n, (unsigned)(n * 1000 / SAMPLE_RATE), s_rec_path,
             esp_err_to_name(err));
    s_rec_err = err;
    s_rec_lvl = 0;
    s_rec_on = false;
}

/* Phat WAV ghi am (16 kHz mono 16-bit, header 44 B): chuan hoa theo muc chung (gain toi da LVL_GAIN_MAX_MIC). */
static void play_wav(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { play_chime(); return; }
    uint8_t h[WAV_HDR];
    if (fread(h, 1, WAV_HDR, f) != WAV_HDR || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); play_chime(); return; }
    uint32_t left = file_left(f, WAV_HDR);
    if (left) play_file(f, left / 2, LVL_GAIN_MAX_MIC);
    fclose(f);
}

static void audio_task(void *arg)
{
    (void)arg;
    s_hw_ok = hw_init() == ESP_OK;
    if (!s_hw_ok) ESP_LOGE(TAG, "khoi tao am thanh that bai");
    fd_tts_init();   /* Pico TTS: nap 1 lan luc boot (PSRAM), lan cham dau khong phai cho */
    lvl_init();
    req_t r;
    while (xQueueReceive(s_q, &r, portMAX_DELAY)) {
        if (!r.pcm && r.count == REQ_RESET) {   /* thuc: reset codec+PA (khong reboot) */
            if (s_tx && s_dev) {
                s_hw_ok = codec_reset("wake") == ESP_OK;
                if (!s_hw_ok) ESP_LOGE(TAG, "reset codec+PA (wake) that bai");
            }
            continue;
        }
        if (!r.pcm && r.count == REQ_REC) { rec_run(); continue; }
        if (r.count == REQ_LONG) ESP_LOGD(TAG, "dequeue: doan dai");
        else ESP_LOGD(TAG, "dequeue: %s %u B", r.text[0] ? r.text : (r.pcm ? "pcm" : "chime"), (unsigned)(r.count * 2));
        if (!s_hw_ok) { free_long(&r); ESP_LOGD(TAG, "skip: hw not ok"); continue; }
        int v = fd_audio_get_volume();
        if (v == 0) { free_long(&r); ESP_LOGD(TAG, "skip: volume==0"); continue; }   /* tat tieng: khong phat, PA giu tat */
        if (v != s_vol_applied) {
            int got = -1;
            esp_err_t e = fd_es8311_voice_volume_set(s_es, v, &got);
            ESP_LOGD(TAG, "es8311 volume_set(%d) -> %d: %s", v, got, esp_err_to_name(e));
            if (e == ESP_OK) s_vol_applied = v;
        } else {
            ESP_LOGD(TAG, "es8311 volume %d (da ap dung)", v);
        }
        s_playing = true;
        amp(true);
        vTaskDelay(pdMS_TO_TICKS(PA_STARTUP_MS));   /* vendor music_test bat PA tu dau; PA can thoi gian khoi dong */
        static const int16_t z[CHUNK] = {0};
        if (r.count == REQ_LONG && r.pcm) play_long((char *)r.pcm);
        else if (!r.pcm && r.count == REQ_WAV) play_wav(r.path);
        else if (r.path[0] || r.flags) {
            for (int k = 0; k < (r.reps ? r.reps : 1); k++) {
                if (k) for (int i = 0; i < 3 * DRAIN_CHUNKS; i++) write_mono(z, CHUNK);   /* nghi ~0.3 s giua 2 lan doc */
                say_word(&r);
            }
        }
        else if (r.text[0]) say_text(r.text);
        else if (r.pcm) { lvl_buffer(r.pcm, r.count, LVL_GAIN_MAX); write_mono(r.pcm, r.count); }
        else play_chime();
        for (int i = 0; i < DRAIN_CHUNKS; i++) write_mono(z, CHUNK);   /* day het DMA (~96 ms) ra loa truoc khi tat PA */
        vTaskDelay(pdMS_TO_TICKS(PA_TAIL_MS));
        amp(false);
        s_playing = false;
        free_long(&r);
    }
}

static esp_err_t start_task(void)   /* lan dau: tao task -> hw_init (reset codec+PA boot) */
{
    if (!s_q) {
        s_q = xQueueCreate(4, sizeof(req_t));
        ESP_RETURN_ON_FALSE(s_q, ESP_ERR_NO_MEM, TAG, "queue");
        if (xTaskCreate(audio_task, "fd_audio", 12288, NULL, 5, NULL) != pdPASS) {
            vQueueDelete(s_q);
            s_q = NULL;
            ESP_LOGE(TAG, "audio task: no internal RAM");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void fd_audio_reset_codec(void)
{
    if (!s_q) { start_task(); return; }   /* boot */
    req_t r = {.pcm = NULL, .count = REQ_RESET};
    if (xQueueSend(s_q, &r, 0) != pdTRUE) ESP_LOGI(TAG, "queue full: drop reset");
}

static esp_err_t post(const int16_t *pcm, size_t n)
{
    ESP_RETURN_ON_ERROR(start_task(), TAG, "start");
    req_t r = {.pcm = pcm, .count = n};
    if (xQueueSend(s_q, &r, 0) != pdTRUE) {
        ESP_LOGI(TAG, "queue full: drop %s %u B", pcm ? "pcm" : "chime", (unsigned)(n * 2));
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGD(TAG, "queue: %s %u B", pcm ? "pcm" : "chime", (unsigned)(n * 2));
    return ESP_OK;
}

void fd_audio_power_off(void)
{
    if (s_hw_ok && s_es) {   /* codec im lang (I2C toi ES8311 duy nhat) */
        esp_err_t e = fd_es8311_voice_mute(s_es, true);
        ESP_LOGD(TAG, "es8311 mute: %s", esp_err_to_name(e));
    }
    amp(false);                                               /* PA tat */
    gpio_hold_en(BOARD_AMP_EN);   /* 1.0.2: giu PA tat suot deep sleep (khong tha noi); hw_init nha lai */
    s_hw_ok = false;                                           /* khong phat nua */
}


esp_err_t fd_audio_beep(void) { return post(NULL, 0); }
esp_err_t fd_audio_play_pcm(const int16_t *samples, size_t count)
{
    ESP_RETURN_ON_FALSE(samples && count, ESP_ERR_INVALID_ARG, TAG, "pcm");
    return post(samples, count);
}
esp_err_t fd_audio_say_long(const char *text)
{
    ESP_RETURN_ON_FALSE(text && text[0], ESP_ERR_INVALID_ARG, TAG, "long");
    ESP_RETURN_ON_ERROR(start_task(), TAG, "start");
    size_t n = strlen(text);
    if (n > 1400) n = 1400;
    while (n > 0 && ((unsigned char)text[n - 1] & 0xC0) == 0x80) n--;
    char *copy = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) copy = malloc(n + 1);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, text, n);
    copy[n] = 0;
    req_t r = {.pcm = (const int16_t *)copy, .count = REQ_LONG};
    if (xQueueSend(s_q, &r, 0) != pdTRUE) {
        free(copy);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void fd_audio_say_stop(void) { s_say_stop = true; }

esp_err_t fd_audio_say(const char *text)
{
    ESP_RETURN_ON_FALSE(text && text[0], ESP_ERR_INVALID_ARG, TAG, "text");
    ESP_RETURN_ON_ERROR(start_task(), TAG, "start");
    req_t r = {.pcm = NULL, .count = 0};
    strlcpy(r.text, text, sizeof(r.text));
    if (xQueueSend(s_q, &r, 0) != pdTRUE) {
        ESP_LOGI(TAG, "queue full: drop tts");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
esp_err_t fd_audio_say_ex(const char *path, const char *text, unsigned flags, int reps)
{
    ESP_RETURN_ON_FALSE(text && text[0] && (flags || (path && path[0])), ESP_ERR_INVALID_ARG, TAG, "arg");
    ESP_RETURN_ON_ERROR(start_task(), TAG, "start");
    req_t r = {.pcm = NULL, .count = 0, .flags = (uint8_t)flags, .reps = (uint8_t)(reps > 1 ? 2 : 1)};
    strlcpy(r.text, text, sizeof(r.text));
    if (path) strlcpy(r.path, path, sizeof(r.path));
    if (xQueueSend(s_q, &r, 0) != pdTRUE) {
        ESP_LOGI(TAG, "queue full: drop voice");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
esp_err_t fd_audio_say_cached(const char *path, const char *text) { return fd_audio_say_ex(path, text, 0, 1); }

esp_err_t fd_audio_rec_start(const char *path, uint32_t max_ms)
{
    ESP_RETURN_ON_FALSE(path && path[0] && strlen(path) < sizeof(s_rec_path) && max_ms >= 200 && max_ms <= 30000,
                        ESP_ERR_INVALID_ARG, TAG, "rec arg");
    if (s_rec_on) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(start_task(), TAG, "start");
    strlcpy(s_rec_path, path, sizeof(s_rec_path));
    s_rec_max = max_ms * (SAMPLE_RATE / 1000);
    s_rec_n = 0;
    s_rec_lvl = 0;
    s_rec_stop = false;
    s_rec_err = ESP_OK;
    s_rec_on = true;
    req_t r = {.pcm = NULL, .count = REQ_REC};
    if (xQueueSend(s_q, &r, 0) != pdTRUE) { s_rec_on = false; s_rec_err = ESP_ERR_TIMEOUT; return ESP_ERR_TIMEOUT; }
    return ESP_OK;
}
void fd_audio_rec_stop(void) { s_rec_stop = true; }
bool fd_audio_rec_active(void) { return s_rec_on; }
uint32_t fd_audio_rec_ms(void) { return s_rec_n * 1000 / SAMPLE_RATE; }
int fd_audio_rec_level(void) { return s_rec_lvl; }
esp_err_t fd_audio_rec_result(void) { return s_rec_err; }
esp_err_t fd_audio_play_wav(const char *path)
{
    ESP_RETURN_ON_FALSE(path && path[0] && strlen(path) < sizeof(((req_t *)0)->path), ESP_ERR_INVALID_ARG, TAG, "wav arg");
    ESP_RETURN_ON_ERROR(start_task(), TAG, "start");
    req_t r = {.pcm = NULL, .count = REQ_WAV};
    strlcpy(r.path, path, sizeof(r.path));
    if (xQueueSend(s_q, &r, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

void fd_audio_set_miss_cb(void (*cb)(const char *text)) { s_miss_cb = cb; }
void fd_audio_set_lookup_cb(bool (*cb)(const char *word, char *path, size_t plen, uint32_t *off, uint32_t *len)) { s_lookup_cb = cb; }

/* Am phan hoi tra loi (tong hop luc chay, bo dem PSRAM giu suot doi):
 * dung = "ting" sang 2 not E6 -> A6 (~200 ms), sai = buzz tram 2 not giam 330 -> 220 Hz (~280 ms, sin + bac 3). */
static int16_t *s_fb[2];
static size_t s_fb_n[2];

static int16_t *synth_fb(bool ok, size_t *out_n)
{
    const int n1 = SAMPLE_RATE * (ok ? 80 : 130) / 1000, n2 = SAMPLE_RATE * (ok ? 120 : 150) / 1000;
    const float f1 = ok ? 1318.5f : 330.0f, f2 = ok ? 1760.0f : 220.0f;
    const float a0 = lvl_tone_amp();   /* 4.6.10: gan muc chung; khi phat van chuan hoa ca doan (r.pcm) */
    size_t n = (size_t)(n1 + n2);
    int16_t *b = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    /* Feedback PCM is non-DMA; leave internal RAM for the I2S staging/rings. */
    if (!b) return NULL;
    for (size_t i = 0; i < n; i++) {
        bool second = (int)i >= n1;
        int k = second ? (int)i - n1 : (int)i, len = second ? n2 : n1;
        float f = second ? f2 : f1, ph = 2.0f * (float)M_PI * f * k / SAMPLE_RATE;
        float env = k < 80 ? k / 80.0f : expf(-(ok ? 4.0f : 2.0f) * (float)(k - 80) / len);
        float v = ok ? sinf(ph) : 0.75f * sinf(ph) + 0.25f * sinf(3.0f * ph);
        b[i] = (int16_t)(a0 * env * v);
    }
    *out_n = n;
    return b;
}

esp_err_t fd_audio_feedback(bool correct)
{
    int i = correct ? 0 : 1;
    if (!s_fb[i]) s_fb[i] = synth_fb(correct, &s_fb_n[i]);
    ESP_RETURN_ON_FALSE(s_fb[i], ESP_ERR_NO_MEM, TAG, "feedback");
    return post(s_fb[i], s_fb_n[i]);
}
