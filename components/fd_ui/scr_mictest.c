/* 2.1.8 Cai dat > Thu micro: ghi am 5 s tu mic tren may (ES8311, tu dung), luu /data/mictest.wav (1 file, ghi de),
 * hien thoi luong + thanh muc am khi ghi; [Ghi am/Dung] [Nghe lai] [Xoa]. Kiem tra dung luong trong truoc khi ghi.
 * Nghe lai qua fd_audio_play_wav (am luong > 60% giam ve ~60% de tranh re).
 * 2.2.3: chuyen vao Che do nha phat trien, doi ten "Thu loa & micro"; thu loa: [Am 1 kHz] / [Quet tan so] theo am luong
 * he thong (DAC toi da 0 dB do fd_audio gioi han). 2.2.7: xem ghi chu "Thu loa" ben duoi. */
#include <stdio.h>
#include <sys/stat.h>
#include "fd_audio.h"
#include "fd_fonts.h"
#include "fd_store.h"
#include "esp_heap_caps.h"
#include "ui_priv.h"
#include <math.h>

#define MIC_PATH    FD_STORE_BASE_PATH "/mictest.wav"
#define MIC_MAX_MS  5000
#define MIC_BYTES   (44 + MIC_MAX_MS * 32)   /* 16 kHz x 2 B */
#define MIC_MARGIN  (16 * 1024)

static lv_obj_t *s_status, *s_bar, *s_rec_btn, *s_rec_ico, *s_rec_txt, *s_play_btn, *s_del_btn;
static lv_timer_t *s_tm;
static bool s_was_rec;

/* ---------- Thu loa ----------
 * 2.2.7: KHONG dung luong fd_audio_stream_* nua. Thiet bi 2.2.3 bi reset khi het am thu: "assert failed" trong tlsf_free
 * <- heap_caps_free <- vStreamBufferGenericDeleteWithCaps <- stream_run (fd_audio.c), tuc hong heap PSRAM khi xoa
 * StreamBuffer luc ket thuc luong. Thay bang PCM sinh san 1 lan vao PSRAM (khong bao gio giai phong) va phat qua
 * fd_audio_play_pcm (cung duong da on dinh voi tieng bip/phat am). Am ngan nen khong can nut Dung. */
#define SPK_RATE   16000
#define SPK_AMP    4000.0f            /* ~ -18 dBFS */
#define SPK_FADE   480                /* 30 ms vao/ra */
#define SPK_TONE_N (SPK_RATE * 3 / 2)  /* 1,5 s: 3 bip 0,3 s, nghi 0,2 s */
#define SPK_SWEEP_N (SPK_RATE * 3)     /* 3 s: 200 Hz -> 4 kHz */
static lv_obj_t *s_spk_tone, *s_spk_sweep, *s_spk_status;
static bool s_spk_alive;
static int16_t *s_tone_pcm, *s_sweep_pcm;

static float fade(int k, int n)
{
    if (k < SPK_FADE) return (float)k / SPK_FADE;
    if (k > n - SPK_FADE) return (float)(n - k) / SPK_FADE;
    return 1.0f;
}

static bool spk_prepare(void)
{
    if (!s_tone_pcm) {
        int16_t *p = heap_caps_malloc(SPK_TONE_N * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!p) return false;
        const int per = SPK_RATE / 2, on = SPK_RATE * 3 / 10;
        for (int k = 0; k < SPK_TONE_N; k++) {
            int q = k % per;
            float env = q < on ? fade(q, on) : 0.0f;
            p[k] = (int16_t)(SPK_AMP * env * sinf(2.0f * (float)M_PI * 1000.0f * k / SPK_RATE));
        }
        s_tone_pcm = p;
    }
    if (!s_sweep_pcm) {
        int16_t *p = heap_caps_malloc(SPK_SWEEP_N * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!p) return false;
        float ph = 0;
        for (int k = 0; k < SPK_SWEEP_N; k++) {
            float f = 200.0f * powf(20.0f, (float)k / SPK_SWEEP_N);   /* log 200 Hz -> 4 kHz */
            ph += 2.0f * (float)M_PI * f / SPK_RATE;
            if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
            p[k] = (int16_t)(SPK_AMP * fade(k, SPK_SWEEP_N) * sinf(ph));
        }
        s_sweep_pcm = p;
    }
    return true;
}

static void spk_start(int mode)
{
    if (s_spk_alive || fd_audio_rec_active() || fd_audio_busy()) return;
    if (fd_audio_get_volume() == 0) { fd_ui_toast(_("MIC_MUTED")); return; }
    if (!spk_prepare()) { fd_ui_toast(_("MIC_FAILED")); return; }
    esp_err_t e = mode ? fd_audio_play_pcm(s_sweep_pcm, SPK_SWEEP_N) : fd_audio_play_pcm(s_tone_pcm, SPK_TONE_N);
    if (e != ESP_OK) { fd_ui_toast(_("MIC_FAILED")); return; }
    s_spk_alive = true;
}

static void spk_tone_cb(lv_event_t *e) { (void)e; spk_start(0); }
static void spk_sweep_cb(lv_event_t *e) { (void)e; spk_start(1); }

static long file_size(void)
{
    struct stat st;
    return stat(MIC_PATH, &st) == 0 ? (long)st.st_size : -1;
}

static void set_enabled(lv_obj_t *b, bool on)
{
    if (on) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
}

static void refresh(void)
{
    if (!s_status) return;
    if (s_spk_alive && !fd_audio_busy()) s_spk_alive = false;   /* phat xong */
    bool spk = s_spk_alive;
    set_enabled(s_spk_tone, !spk && !fd_audio_rec_active() && !fd_audio_busy());
    set_enabled(s_spk_sweep, !spk && !fd_audio_rec_active() && !fd_audio_busy());
    lv_label_set_text(s_spk_status, spk ? _("SPK_PLAYING") : "");
    bool rec = fd_audio_rec_active(), play = fd_audio_is_playing() || spk;
    long sz = file_size();
    if (rec) {
        unsigned ms = (unsigned)fd_audio_rec_ms();
        lv_label_set_text_fmt(s_status, "%s %u.%u / %d s", _("MIC_RECORDING"), ms / 1000, (ms % 1000) / 100, MIC_MAX_MS / 1000);
        lv_bar_set_value(s_bar, fd_audio_rec_level(), LV_ANIM_OFF);
    } else {
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        if (play) lv_label_set_text(s_status, _("MIC_PLAYING"));
        else if (sz > 44) {
            unsigned ms = (unsigned)((sz - 44) / 32);
            lv_label_set_text_fmt(s_status, _("MIC_SAVED"), (int)(ms / 1000), (int)((ms % 1000) / 100));
        } else lv_label_set_text(s_status, _("MIC_NO_FILE"));
    }
    lv_label_set_text(s_rec_ico, rec ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
    lv_label_set_text(s_rec_txt, rec ? _("MIC_STOP") : _("MIC_RECORD"));
    set_enabled(s_rec_btn, rec || !play);
    set_enabled(s_play_btn, !rec && !play && sz > 44);
    set_enabled(s_del_btn, !rec && !play && sz >= 0);
}

static void tm_cb(lv_timer_t *t)
{
    (void)t;
    bool rec = fd_audio_rec_active();
    if (s_was_rec && !rec) {
        esp_err_t e = fd_audio_rec_result();
        if (e != ESP_OK) fd_ui_toast(e == ESP_ERR_NO_MEM ? _("MIC_NO_SPACE") : _("MIC_FAILED"));
    }
    s_was_rec = rec;
    refresh();
}

static void rec_cb(lv_event_t *e)
{
    (void)e;
    if (fd_audio_rec_active()) { fd_audio_rec_stop(); return; }
    if (fd_audio_busy() || s_spk_alive) return;
    size_t total = 0, used = 0;
    long old = file_size();
    if (fd_store_info(&total, &used) != ESP_OK ||
        (long long)total - (long long)used + (old > 0 ? old : 0) < (long long)(MIC_BYTES + MIC_MARGIN)) {
        fd_ui_toast(_("MIC_NO_SPACE"));
        return;
    }
    if (fd_audio_rec_start(MIC_PATH, MIC_MAX_MS) != ESP_OK) { fd_ui_toast(_("MIC_FAILED")); return; }
    s_was_rec = true;
    refresh();
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    if (fd_audio_rec_active() || fd_audio_busy() || s_spk_alive || file_size() <= 44) return;
    if (fd_audio_get_volume() == 0) { fd_ui_toast(_("MIC_MUTED")); return; }
    fd_audio_play_wav(MIC_PATH);
    refresh();
}

static void del_cb(lv_event_t *e)
{
    (void)e;
    if (fd_audio_rec_active() || fd_audio_is_playing()) return;
    remove(MIC_PATH);
    refresh();
}

static lv_obj_t *act_btn(lv_obj_t *parent, const char *sym, const char *txt, lv_event_cb_t cb, lv_obj_t **ico, lv_obj_t **lbl)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(31), 64);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *i = lv_label_create(b);
    lv_obj_set_style_text_font(i, &lv_font_montserrat_14, 0);   /* 2.2.6: Noto khong co LV_SYMBOL_* (o vuong) */
    lv_label_set_text(i, sym);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_label_set_text(l, txt);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if (ico) *ico = i;
    if (lbl) *lbl = l;
    return b;
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_hor(body, 10, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_set_scrollable(body, true);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(body);
    lv_obj_set_style_text_font(t, &fd_font_noto_18, 0);
    lv_obj_set_width(t, lv_pct(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, _("MIC_TEST"));

    lv_obj_t *sh = lv_label_create(body);   /* --- Loa --- */
    lv_obj_set_style_text_color(sh, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(sh, lv_pct(100));
    lv_label_set_text(sh, _("SPK_TEST"));
    lv_obj_t *srow = lv_obj_create(body);
    lv_obj_remove_style_all(srow);
    lv_obj_set_size(srow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_spk_tone = act_btn(srow, LV_SYMBOL_BELL, _("SPK_TONE"), spk_tone_cb, NULL, NULL);
    s_spk_sweep = act_btn(srow, LV_SYMBOL_SHUFFLE, _("SPK_SWEEP"), spk_sweep_cb, NULL, NULL);
    s_spk_status = lv_label_create(body);
    lv_obj_set_style_text_color(s_spk_status, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);

    lv_obj_t *mh = lv_label_create(body);   /* --- Micro --- */
    lv_obj_set_style_text_color(mh, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(mh, lv_pct(100));
    lv_label_set_text(mh, _("MIC_SECTION"));

    lv_obj_t *h = lv_label_create(body);
    lv_obj_set_style_text_font(h, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(h, lv_pct(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(h, _("MIC_HINT"));

    s_status = lv_label_create(body);
    lv_obj_set_style_text_font(s_status, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    lv_obj_set_width(s_status, lv_pct(100)); lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);

    s_bar = lv_bar_create(body);   /* muc am khi ghi (0..100 ~ -60..0 dB) */
    lv_obj_set_width(s_bar, lv_pct(100));
    lv_obj_set_height(s_bar, 14);
    lv_bar_set_range(s_bar, 0, 100);

    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_rec_btn = act_btn(row, LV_SYMBOL_AUDIO, _("MIC_RECORD"), rec_cb, &s_rec_ico, &s_rec_txt);
    s_play_btn = act_btn(row, LV_SYMBOL_PLAY, _("MIC_PLAY"), play_cb, NULL, NULL);
    s_del_btn = act_btn(row, LV_SYMBOL_TRASH, _("MIC_DELETE"), del_cb, NULL, NULL);

    s_was_rec = fd_audio_rec_active();
    s_tm = lv_timer_create(tm_cb, 100, NULL);
    refresh();
}

static void destroy(void)
{
    if (s_tm) { lv_timer_delete(s_tm); s_tm = NULL; }
    if (fd_audio_rec_active()) fd_audio_rec_stop();   /* roi man: dung ghi (van luu phan da ghi) */
    s_spk_tone = s_spk_sweep = s_spk_status = NULL;
    s_status = s_bar = s_rec_btn = s_rec_ico = s_rec_txt = s_play_btn = s_del_btn = NULL;
}

const ui_screen_t g_scr_mictest = {
    .name = "mictest", .parent = FD_SCR_DEVMODE, .build = build, .destroy = destroy,
};