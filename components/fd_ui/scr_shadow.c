/* 2.0.1 Shadowing: trinh phat video toan man (ngang 320x240).
 * Server (GET /api/devices/media/stream) chuyen YouTube -> luong FDMS v1: [type u8][len u32 LE][payload]
 *   0 INFO (16 B: "FDMS" ver w h fps rate ch bits) | 1 VIDEO (1 JPEG baseline) | 2 AUDIO (s16le mono 16 kHz) | 3 END | 4 ERROR (chuoi)
 * Task mang: doc luong, PCM -> fd_audio_stream (bo dem jitter ~0.8 s), JPEG -> hang doi.
 * Task hinh: giai ma JPEG (ROM tjpgd) vao bo dem PSRAM doi (2 x 320x240 RGB565), hien khung n khi dong ho am thanh >= n/fps.
 * Cham man hinh / vuot mep = thoat. Khong giu lock LVGL khi giai ma / doc mang. */
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fd_audio.h"
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rom/tjpgd.h"
#include "ui_priv.h"

static const char *TAG = "shadow";

/* Video co dinh cua 2.0.1 (https://www.youtube.com/watch?v=Gp340_5dT5g), da ma hoa URL */
#define SHADOW_URL_ENC "https%3A%2F%2Fwww.youtube.com%2Fwatch%3Fv%3DGp340_5dT5g"
#define VW 320
#define VH 240
#define MAX_FRAME (96 * 1024)
#define VQ_DEPTH 24                 /* ~2.4 s khung cho (am thanh tre ~0.8 s so voi mang) */
#define AUDIO_BUF_SAMPLES (16000 * 3)
#define AUDIO_PRE_SAMPLES (16000 * 8 / 10)

enum { FT_INFO = 0, FT_VIDEO = 1, FT_AUDIO = 2, FT_END = 3, FT_ERROR = 4 };

typedef struct {
    uint8_t *jpg;   /* NULL = het luong */
    uint32_t len;
    uint32_t idx;
} vframe_t;

typedef struct {
    volatile bool stop;
    int refs;
    lv_obj_t *img, *msg;            /* chi doc/ghi khi giu lock LVGL; destroy dat NULL */
    uint16_t *fb[2];
    lv_image_dsc_t dsc[2];
    int cur;
    QueueHandle_t q;
    /* parser */
    uint8_t hdr[5];
    int hlen;
    uint8_t type;
    uint32_t need, got;
    uint8_t *pl;
    /* trang thai */
    bool info_ok, audio_on, have_audio, ended;
    int fps;
    uint32_t vidx;
    int code;
    char err[48];
    uint8_t work[3100];
} shadow_t;

static shadow_t *s_ctx;
static int s_prev_rot = -1;

static void ctx_release(shadow_t *c)
{
    if (__atomic_sub_fetch(&c->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    vframe_t f;
    while (c->q && xQueueReceive(c->q, &f, 0) == pdTRUE) free(f.jpg);
    if (c->q) vQueueDelete(c->q);
    heap_caps_free(c->fb[0]);
    heap_caps_free(c->fb[1]);
    heap_caps_free(c->pl);
    heap_caps_free(c);
}

/* Hien thong bao (giu lock). txt NULL = an. */
static void show_msg(shadow_t *c, const char *txt)
{
    if (!fd_display_lock(1000)) return;
    if (c->msg) {
        if (txt) { lv_label_set_text(c->msg, txt); lv_obj_set_hidden(c->msg, false); }
        else lv_obj_set_hidden(c->msg, true);
    }
    fd_display_unlock();
}

/* ---------- JPEG (ROM TJpgDec, ra RGB888) -> RGB565 ---------- */
typedef struct {
    const uint8_t *src;
    uint32_t len, pos;
    uint16_t *dst;
    int ox, oy;
} jdev_t;

static UINT jin(JDEC *jd, BYTE *buf, UINT n)
{
    jdev_t *d = (jdev_t *)jd->device;
    if (n > d->len - d->pos) n = d->len - d->pos;
    if (buf) memcpy(buf, d->src + d->pos, n);
    d->pos += n;
    return n;
}

static UINT jout(JDEC *jd, void *bitmap, JRECT *r)
{
    jdev_t *d = (jdev_t *)jd->device;
    const uint8_t *p = (const uint8_t *)bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        int yy = y + d->oy;
        for (int x = r->left; x <= r->right; x++, p += 3) {
            int xx = x + d->ox;
            if ((unsigned)xx < VW && (unsigned)yy < VH)
                d->dst[yy * VW + xx] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        }
    }
    return 1;
}

static bool decode(shadow_t *c, const uint8_t *jpg, uint32_t len, uint16_t *dst)
{
    JDEC jd;
    jdev_t dv = {.src = jpg, .len = len, .dst = dst};
    if (jd_prepare(&jd, jin, c->work, sizeof(c->work), &dv) != JDR_OK) return false;
    dv.ox = ((int)VW - (int)jd.width) / 2;
    dv.oy = ((int)VH - (int)jd.height) / 2;
    return jd_decomp(&jd, jout, 0) == JDR_OK;
}

/* ---------- task hinh ---------- */
static uint32_t clock_ms(shadow_t *c, int64_t *t0)
{
    if (c->have_audio && fd_audio_stream_active()) return (uint32_t)((uint64_t)fd_audio_stream_played() * 1000 / 16000);
    if (!*t0) *t0 = esp_timer_get_time();
    return (uint32_t)((esp_timer_get_time() - *t0) / 1000);
}

static void video_task(void *arg)
{
    shadow_t *c = arg;
    int64_t t0 = 0, last_show = 0;
    uint32_t shown = 0, dropped = 0;
    bool first = true;
    vframe_t f;
    while (!c->stop) {
        if (xQueueReceive(c->q, &f, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (!f.jpg) break;   /* het luong */
        uint32_t target = f.idx * 1000u / (uint32_t)(c->fps > 0 ? c->fps : 10);
        bool audio_alive = c->have_audio && fd_audio_stream_active();
        while (!c->stop && clock_ms(c, &t0) < target && (audio_alive || !c->have_audio)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            audio_alive = fd_audio_stream_active();
        }
        int64_t now = esp_timer_get_time();
        if (!c->stop && clock_ms(c, &t0) > target + 200 && !first && now - last_show < 1000000) {
            dropped++;   /* tre > 2 khung: bo (van hien it nhat 1 khung/s) */
            free(f.jpg);
            continue;
        }
        int back = c->cur ^ 1;
        bool ok = !c->stop && decode(c, f.jpg, f.len, c->fb[back]);
        free(f.jpg);
        if (!ok) { dropped++; continue; }
        if (fd_display_lock(500)) {
            if (c->img) {
                /* CONFIG_LV_CACHE_DEF_SIZE=0: khong co cache anh, doi src la du */
                lv_image_set_src(c->img, &c->dsc[back]);
                lv_obj_invalidate(c->img);
                if (first && c->msg) lv_obj_set_hidden(c->msg, true);
            }
            fd_display_unlock();
            c->cur = back;
            first = false;
            shown++;
            last_show = now;
        }
    }
    while (!c->stop && fd_audio_stream_active()) vTaskDelay(pdMS_TO_TICKS(50));   /* cho am thanh phat not */
    ESP_LOGI(TAG, "video: hien %u, bo %u khung", (unsigned)shown, (unsigned)dropped);
    if (!c->stop) {
        char m[96];
        if (c->ended && !c->err[0]) snprintf(m, sizeof(m), "%s\n%s", _("SHADOW_END"), _("SHADOW_TAP_EXIT"));
        else if (c->code == 401) snprintf(m, sizeof(m), "%s", _("SHADOW_NOT_PAIRED"));
        else if (c->code == 429 || c->code == 503) snprintf(m, sizeof(m), "%s", _("SHADOW_BUSY"));
        else if (c->code < 0) snprintf(m, sizeof(m), "%s", _("SHADOW_NET_ERROR"));
        else snprintf(m, sizeof(m), _("SHADOW_ERROR"), c->err[0] ? c->err : "http");
        if (c->code > 0 && c->code != 200 && !c->err[0]) {
            size_t l = strlen(m);
            snprintf(m + l, sizeof(m) - l, " (%d)", c->code);
        }
        show_msg(c, m);
    }
    ctx_release(c);
    vTaskDelete(NULL);
}

/* ---------- task mang + parser FDMS ---------- */
static bool on_frame(shadow_t *c)
{
    const uint8_t *p = c->pl;
    uint32_t n = c->need;
    switch (c->type) {
    case FT_INFO:
        if (n < 16 || memcmp(p, "FDMS", 4) || p[4] != 1) { strlcpy(c->err, "info", sizeof(c->err)); return false; }
        {
            uint32_t rate = p[10] | (p[11] << 8) | (p[12] << 16) | ((uint32_t)p[13] << 24);
            c->fps = p[9] ? p[9] : 10;
            if (rate != 16000 || p[14] != 1 || p[15] != 16) { strlcpy(c->err, "format", sizeof(c->err)); return false; }
            ESP_LOGI(TAG, "info: %ux%u %d fps, %u Hz", p[5] | (p[6] << 8), p[7] | (p[8] << 8), c->fps, (unsigned)rate);
        }
        c->info_ok = true;
        if (fd_audio_stream_begin(AUDIO_BUF_SAMPLES, AUDIO_PRE_SAMPLES) == ESP_OK) c->audio_on = true;
        else ESP_LOGW(TAG, "audio stream ban: chi hinh");
        return true;
    case FT_VIDEO: {
        if (!c->info_ok) return true;
        vframe_t f = {.len = n, .idx = c->vidx++};
        f.jpg = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (!f.jpg) return true;
        memcpy(f.jpg, p, n);
        if (xQueueSend(c->q, &f, 0) != pdTRUE) free(f.jpg);   /* hang doi day: bo khung */
        return true;
    }
    case FT_AUDIO:
        if (c->audio_on && n >= 2) {
            c->have_audio = true;
            fd_audio_stream_write((const int16_t *)p, n / 2, 2000);
        }
        return true;
    case FT_END:
        c->ended = true;
        return false;
    case FT_ERROR: {
        uint32_t k = n < sizeof(c->err) - 1 ? n : sizeof(c->err) - 1;
        memcpy(c->err, p, k);
        c->err[k] = 0;
        ESP_LOGW(TAG, "server error: %s", c->err);
        return false;
    }
    default:
        return true;   /* loai la: bo qua */
    }
}

static bool on_data(const uint8_t *buf, int len, void *ctx)
{
    shadow_t *c = ctx;
    while (len > 0) {
        if (c->stop) return false;
        if (c->hlen < 5) {
            c->hdr[c->hlen++] = *buf++;
            len--;
            if (c->hlen == 5) {
                c->type = c->hdr[0];
                c->need = c->hdr[1] | (c->hdr[2] << 8) | (c->hdr[3] << 16) | ((uint32_t)c->hdr[4] << 24);
                c->got = 0;
                if (c->need > MAX_FRAME) { strlcpy(c->err, "frame", sizeof(c->err)); return false; }
                if (c->need == 0) { c->hlen = 0; if (!on_frame(c)) return false; }
            }
            continue;
        }
        uint32_t k = c->need - c->got;
        if (k > (uint32_t)len) k = (uint32_t)len;
        memcpy(c->pl + c->got, buf, k);
        c->got += k;
        buf += k;
        len -= (int)k;
        if (c->got == c->need) {
            c->hlen = 0;
            if (!on_frame(c)) return false;
        }
    }
    return !c->stop;
}

static void net_task(void *arg)
{
    shadow_t *c = arg;
    char id[16], path[200];
    fd_ota_get_device_id(id, sizeof(id));
    snprintf(path, sizeof(path), "/api/devices/media/stream?device_id=%s&url=%s", id, SHADOW_URL_ENC);
    c->code = fd_ota_api_get_stream(path, 8000, on_data, c);
    if (c->audio_on) fd_audio_stream_end(c->stop || !c->ended);
    vframe_t end = {0};
    while (!c->stop && xQueueSend(c->q, &end, pdMS_TO_TICKS(200)) != pdTRUE) {}
    if (c->stop) xQueueSend(c->q, &end, 0);
    ESP_LOGI(TAG, "net: HTTP %d, %u khung hinh%s", c->code, (unsigned)c->vidx, c->ended ? ", END" : "");
    ctx_release(c);
    vTaskDelete(NULL);
}

/* ---------- man hinh ---------- */
static void tap_cb(lv_event_t *e)
{
    (void)e;
    ui_nav_back();
}

static void build_shadow(lv_obj_t *scr)
{
    s_prev_rot = -1;
    if (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) {
        s_prev_rot = fd_display_get_rotation();   /* doc: tam xoay ngang, tra lai khi thoat (khong luu NVS) */
        fd_display_set_rotation(0, false);
        lv_obj_set_size(scr, VW, VH);
    }
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_clickable(scr, true);
    lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *msg = lv_label_create(scr);
    lv_obj_set_width(msg, VW - 24);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(msg, lv_color_white(), 0);
    lv_obj_set_style_text_font(msg, &fd_font_noto_18, 0);
    lv_obj_center(msg);

    if (!fd_net_is_connected()) { lv_label_set_text(msg, _("SHADOW_NO_WIFI")); return; }
    if (!fd_ota_is_paired()) { lv_label_set_text(msg, _("SHADOW_NOT_PAIRED")); return; }

    shadow_t *c = heap_caps_calloc(1, sizeof(*c), MALLOC_CAP_SPIRAM);
    if (c) {
        c->fb[0] = heap_caps_calloc(VW * VH, 2, MALLOC_CAP_SPIRAM);
        c->fb[1] = heap_caps_calloc(VW * VH, 2, MALLOC_CAP_SPIRAM);
        c->pl = heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM);
        c->q = xQueueCreate(VQ_DEPTH, sizeof(vframe_t));
        c->fps = 10;
    }
    if (!c || !c->fb[0] || !c->fb[1] || !c->pl || !c->q) {
        if (c) { c->refs = 1; ctx_release(c); }
        lv_label_set_text_fmt(msg, _("SHADOW_ERROR"), "mem");
        return;
    }
    for (int i = 0; i < 2; i++) {
        lv_image_dsc_t *d = &c->dsc[i];
        d->header.magic = LV_IMAGE_HEADER_MAGIC;
        d->header.cf = LV_COLOR_FORMAT_RGB565;
        d->header.w = VW;
        d->header.h = VH;
        d->header.stride = VW * 2;
        d->data_size = VW * VH * 2;
        d->data = (const uint8_t *)c->fb[i];
    }
    lv_obj_t *img = lv_image_create(scr);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_clickable(img, false);   /* cham xuyen xuong screen = thoat */
    lv_image_set_src(img, &c->dsc[0]);
    lv_obj_move_background(img);
    c->img = img;
    c->msg = msg;
    lv_label_set_text(msg, _("SHADOW_LOADING"));
    c->refs = 2;
    if (xTaskCreate(net_task, "shadow_net", 8192, c, 5, NULL) != pdPASS) {
        c->refs = 1; ctx_release(c);
        lv_label_set_text_fmt(msg, _("SHADOW_ERROR"), "task");
        return;
    }
    if (xTaskCreate(video_task, "shadow_vid", 6144, c, 4, NULL) != pdPASS) {
        c->stop = true;   /* net_task tu thoat va giai phong */
        c->img = c->msg = NULL;
        ctx_release(c);
        lv_label_set_text_fmt(msg, _("SHADOW_ERROR"), "task");
        return;
    }
    s_ctx = c;
}

static void destroy_shadow(void)
{
    if (s_ctx) {
        s_ctx->stop = true;   /* 2 task tu dung + giai phong (khong cho o day: dang giu lock) */
        s_ctx->img = NULL;
        s_ctx->msg = NULL;
        s_ctx = NULL;
    }
    if (s_prev_rot >= 0) {
        fd_display_set_rotation(s_prev_rot, false);
        s_prev_rot = -1;
    }
}

const ui_screen_t g_scr_shadow = {.name = "shadow", .parent = FD_SCR_HOME, .build = build_shadow, .destroy = destroy_shadow};
