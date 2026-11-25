/* 4.9.0 May ao Berry cho tro choi tai ve. Xem include/fd_berry.h. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "berry.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "fd_berry.h"

static const char *TAG = "fd_berry";

#define STACK_BYTES (24 * 1024)
#define HDR 8   /* dau moi khoi: kich thuoc (giu canh 8 byte) */

/* ---------- bo nho: PSRAM, co tran ---------- */
static size_t s_mem, s_mem_peak;

void *fd_berry_malloc(size_t n)
{
    if (n == 0) return NULL;
    if (s_mem + n + HDR > FD_BERRY_MEM_CAP) return NULL;
    uint8_t *p = heap_caps_malloc(n + HDR, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) return NULL;
    *(size_t *)p = n;
    s_mem += n + HDR;
    if (s_mem > s_mem_peak) s_mem_peak = s_mem;
    return p + HDR;
}

void fd_berry_free(void *ptr)
{
    if (!ptr) return;
    uint8_t *p = (uint8_t *)ptr - HDR;
    s_mem -= *(size_t *)p + HDR;
    heap_caps_free(p);
}

void *fd_berry_realloc(void *ptr, size_t n)
{
    if (!ptr) return fd_berry_malloc(n);
    if (n == 0) { fd_berry_free(ptr); return NULL; }
    uint8_t *p = (uint8_t *)ptr - HDR;
    size_t old = *(size_t *)p;
    if (n > old && s_mem + (n - old) > FD_BERRY_MEM_CAP) return NULL;
    uint8_t *q = heap_caps_realloc(p, n + HDR, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!q) return NULL;
    *(size_t *)q = n;
    s_mem = s_mem - old + n;
    if (s_mem > s_mem_peak) s_mem_peak = s_mem;
    return q + HDR;
}

/* Berry chi goi abort / exit khi loi ngoai be_pcall (fd_berry luon goi qua be_pcall). */
void fd_berry_abort(void) { ESP_LOGE(TAG, "abort"); abort(); }
void fd_berry_exit(int code) { ESP_LOGE(TAG, "exit %d", code); abort(); }

/* print() cua script -> log theo dong */
static char s_line[128];
static size_t s_line_n;
void fd_berry_print(const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (b[i] == '\n' || s_line_n >= sizeof(s_line) - 1) {
            s_line[s_line_n] = 0;
            ESP_LOGI(TAG, "script: %s", s_line);
            s_line_n = 0;
            if (b[i] == '\n') continue;
        }
        s_line[s_line_n++] = b[i];
    }
}

size_t fd_berry_mem_used(void) { return s_mem; }

/* ---------- trang thai dung chung (VM ghi, LVGL doc) ---------- */
static SemaphoreHandle_t s_mx, s_done;
static QueueHandle_t s_q;
static TaskHandle_t s_task;
static fd_berry_obj_t s_obj[FD_BERRY_OBJ_MAX];
static uint32_t s_dirty;
static fd_berry_misc_t s_misc;
static fd_berry_cfg_t s_cfg;
static volatile bool s_stop, s_paused, s_running;
static volatile int64_t s_deadline;
static int64_t s_t0;
/* 4.9.5 dong ho tro choi (khong dem luc tam dung) + hoat hinh fd.move (tinh trong C) */
static int64_t s_pause_acc, s_pause_at;
typedef struct { bool on; int16_t x0, y0, x1, y1; int64_t t0; int32_t dur; } anim_t;
static anim_t s_anim[FD_BERRY_OBJ_MAX];
static uint32_t s_arrived;   /* bit: doi tuong vua toi dich, cho goi arrive(id) */
/* 5.1.0 hieu ung (fd.fx) va lac (fd.sway), tinh theo dong ho tro choi */
typedef struct { uint8_t kind; int64_t t0; int32_t dur; } fx_t;
typedef struct { int16_t amp; int32_t period; } sway_t;
static fx_t s_fx[FD_BERRY_OBJ_MAX];
static sway_t s_sway[FD_BERRY_OBJ_MAX];
static uint32_t s_calls, s_max_us;
static uint64_t s_sum_us;

typedef struct { uint8_t type; int16_t arg; } ev_t;

/* 5.4.0 tinh nang tai ve: lenh cho man hinh, doan doc dai, pref, khung chu cuon */
#define CMD_Q 16
static fd_berry_cmd_t s_cmd[CMD_Q];
static int s_cmd_h, s_cmd_n;
static char *s_long;                     /* PSRAM FD_BERRY_LONG_MAX */
static volatile bool s_long_pend, s_busy, s_hold_back;
static fd_berry_roll_item_t *s_roll;     /* PSRAM FD_BERRY_ROLL_MAX */
static fd_berry_roll_t s_rst;
static fd_berry_roll_mark_t s_marks[FD_BERRY_MARK_MAX];
static int s_nmark;
static volatile int s_roll_y;
static fd_berry_line_t *s_lines;         /* PSRAM FD_BERRY_LINES, chi task VM */
static const char *const PREF_K[] = {"sh_vi", "sh_ipa", "sh_snd", "sh_spd"};
static const uint8_t PREF_DEF[] = {1, 0, 0, 1};
#define PREF_N ((int)(sizeof(PREF_K) / sizeof(PREF_K[0])))
static uint8_t s_pref[PREF_N];
static uint32_t s_pref_dirty;

/* chep chuoi, cat dung ranh gioi UTF-8 */
static void ucpy(char *d, size_t cap, const char *s)
{
    if (!cap) return;
    size_t n = s ? strlen(s) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(d, s, n);
    d[n] = 0;
}

#define LOCK() xSemaphoreTake(s_mx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mx)

/* goi khi da LOCK */
static int64_t gclock_us(void)
{
    int64_t now = esp_timer_get_time();
    return now - s_t0 - s_pause_acc - (s_pause_at ? now - s_pause_at : 0);
}

static void fx_reset(int i)
{
    s_fx[i].kind = 0;
    s_obj[i].scale = 256;
    s_obj[i].opa_now = s_obj[i].opa;
}

/* 5.1.0: hieu ung + lac cua doi tuong i tai thoi diem t (goi khi da LOCK) */
static void fx_step(int i, int64_t t)
{
    fd_berry_obj_t *o = &s_obj[i];
    fx_t *f = &s_fx[i];
    sway_t *w = &s_sway[i];
    if (!f->kind && !w->amp) return;
    int ox = 0;
    if (w->amp && w->period > 0) {
        float ph = (float)(t % ((int64_t)w->period * 1000)) / ((float)w->period * 1000.0f);
        ox = (int)lroundf((float)w->amp * sinf(6.2831853f * ph + (float)i * 1.7f));
    }
    if (f->kind) {
        float p = f->dur > 0 ? (float)(t - f->t0) / ((float)f->dur * 1000.0f) : 1.0f;
        if (p < 0) p = 0;
        if (p >= 1.0f) {
            if (f->kind == 1 || f->kind == 2) o->vis = false;   /* no / mo dan: an khi xong */
            fx_reset(i);
        } else {
            switch (f->kind) {
            case 1: o->scale = (uint16_t)(256 + 160 * p); o->opa_now = (uint8_t)(o->opa * (1.0f - p)); break;
            case 2: o->opa_now = (uint8_t)(o->opa * (1.0f - p)); break;
            case 3: o->opa_now = (uint8_t)(o->opa * p); break;
            case 4: ox += (int)lroundf(7.0f * sinf(p * 18.849556f) * (1.0f - p)); break;
            case 5: o->scale = (uint16_t)(256 + 48 * sinf(p * 3.1415927f)); break;
            default: fx_reset(i); break;
            }
        }
    }
    o->ox = (int16_t)ox;
    s_dirty |= 1u << i;
}

/* Cap nhat vi tri cac doi tuong dang chay (goi khi da LOCK). */
static void anim_step(void)
{
    int64_t t = gclock_us();
    for (int i = 0; i < FD_BERRY_OBJ_MAX; i++) if (s_obj[i].used) fx_step(i, t);
    for (int i = 0; i < FD_BERRY_OBJ_MAX; i++) {
        anim_t *a = &s_anim[i];
        if (!a->on) continue;
        int64_t el = t - a->t0;
        fd_berry_obj_t *o = &s_obj[i];
        if (el >= (int64_t)a->dur * 1000 || a->dur <= 0) {
            o->x = a->x1;
            o->y = a->y1;
            a->on = false;
            s_arrived |= 1u << i;
        } else {
            int64_t d = (int64_t)a->dur * 1000;
            o->x = (int16_t)(a->x0 + (int64_t)(a->x1 - a->x0) * el / d);
            o->y = (int16_t)(a->y0 + (int64_t)(a->y1 - a->y0) * el / d);
        }
        s_dirty |= 1u << i;
    }
}

static void set_err(const char *a, const char *b)
{
    LOCK();
    if (s_misc.state != FD_BERRY_ERROR) {
        s_misc.state = FD_BERRY_ERROR;
        snprintf(s_misc.err, sizeof(s_misc.err), "%s%s%s", a ? a : "", b ? ": " : "", b ? b : "");
    }
    UNLOCK();
}

/* ---------- API fd (chay trong task VM) ---------- */
static int arg_id(bvm *vm)
{
    if (be_top(vm) < 1 || !be_isint(vm, 1)) be_raise(vm, "type_error", "id phai la so 0..31");
    int id = (int)be_toint(vm, 1);
    if (id < 0 || id >= FD_BERRY_OBJ_MAX) be_raise(vm, "value_error", "id phai trong 0..31");
    return id;
}

static int argi(bvm *vm, int i, int def)
{
    if (be_top(vm) < i) return def;
    if (be_isint(vm, i)) return (int)be_toint(vm, i);
    if (be_isreal(vm, i)) return (int)be_toreal(vm, i);
    return def;
}

static fd_berry_obj_t *touch(int id)
{
    fd_berry_obj_t *o = &s_obj[id];
    if (!o->used) {
        memset(o, 0, sizeof(*o));
        o->used = true;
        o->vis = true;
        o->bg = 0xFFFFFF;
        o->fg = 0x111827;
        o->font = 18;
        o->radius = 10;
        o->pad = 4;
        o->opa = 255;
        o->opa_now = 255;
        o->scale = 256;
        s_fx[id].kind = 0;
        s_sway[id].amp = 0;
    }
    s_dirty |= 1u << id;
    return o;
}

static int f_text(bvm *vm)
{
    int id = arg_id(vm);
    const char *t = be_top(vm) >= 2 ? be_tostring(vm, 2) : "";
    LOCK();
    fd_berry_obj_t *o = touch(id);
    ucpy(o->txt, sizeof(o->txt), t);
    UNLOCK();
    be_return_nil(vm);
}

static int clampv(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int f_box(bvm *vm)
{
    int id = arg_id(vm);
    int x = argi(vm, 2, 0), y = argi(vm, 3, 0), w = argi(vm, 4, 0), h = argi(vm, 5, 0);
    LOCK();
    s_anim[id].on = false;
    s_arrived &= ~(1u << id);
    fd_berry_obj_t *o = touch(id);
    fx_reset(id);   /* 5.1.0: dat lai vi tri = bat dau lai (huy hieu ung no / mo dang chay) */
    o->x = (int16_t)clampv(x, -1000, 2000);
    o->y = (int16_t)clampv(y, -1000, 2000);
    o->w = (int16_t)clampv(w, 0, 640);
    o->h = (int16_t)clampv(h, 0, 640);
    UNLOCK();
    be_return_nil(vm);
}

static int f_color(bvm *vm)
{
    int id = arg_id(vm);
    int bg = argi(vm, 2, 0xFFFFFF), fg = argi(vm, 3, 0x111827);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    o->bg = bg < 0 ? -1 : (bg & 0xFFFFFF);
    o->fg = fg & 0xFFFFFF;
    UNLOCK();
    be_return_nil(vm);
}

static int f_font(bvm *vm)
{
    int id = arg_id(vm);
    int f = argi(vm, 2, 18);
    LOCK();
    touch(id)->font = (uint8_t)(f >= 32 ? 32 : f >= 24 ? 24 : f >= 18 ? 18 : f >= 14 ? 14 : 12);   /* 5.4.5 them 24 */
    UNLOCK();
    be_return_nil(vm);
}

static int f_show(bvm *vm)
{
    int id = arg_id(vm);
    bool v = be_top(vm) < 2 || be_tobool(vm, 2);
    LOCK();
    touch(id)->vis = v;
    UNLOCK();
    be_return_nil(vm);
}

static int f_clear(bvm *vm)
{
    LOCK();
    for (int i = 0; i < FD_BERRY_OBJ_MAX; i++)
        if (s_obj[i].used) { s_obj[i].vis = false; fx_reset(i); s_sway[i].amp = 0; s_obj[i].ox = 0; s_dirty |= 1u << i; }
    UNLOCK();
    be_return_nil(vm);
}

static int f_hud(bvm *vm)
{
    LOCK();
    s_misc.score = argi(vm, 1, 0);
    s_misc.lives = clampv(argi(vm, 2, -1), -1, 9);
    s_misc.level = argi(vm, 3, 0);
    s_misc.combo = argi(vm, 4, 0);
    s_misc.hud_dirty = true;
    UNLOCK();
    be_return_nil(vm);
}

static int f_sound(bvm *vm)
{
    LOCK();
    s_misc.sound = (be_top(vm) < 1 || be_tobool(vm, 1)) ? 1 : 0;
    UNLOCK();
    be_return_nil(vm);
}

static int f_say(bvm *vm)
{
    const char *t = be_top(vm) >= 1 ? be_tostring(vm, 1) : "";
    LOCK();
    strlcpy(s_misc.say, t ? t : "", sizeof(s_misc.say));
    UNLOCK();
    be_return_nil(vm);
}

static int f_over(bvm *vm)
{
    LOCK();
    s_misc.over_score = argi(vm, 1, s_misc.score);
    s_misc.over_right = argi(vm, 2, -1);
    s_misc.over_level = argi(vm, 3, s_misc.level);
    if (s_misc.state == FD_BERRY_RUNNING) s_misc.state = FD_BERRY_OVER;
    UNLOCK();
    s_stop = true;
    be_return_nil(vm);
}

static int f_rand(bvm *vm)
{
    int n = argi(vm, 1, 0);
    be_pushint(vm, n > 0 ? (bint)(esp_random() % (uint32_t)n) : (bint)(esp_random() & 0x7FFFFFFF));
    be_return(vm);
}

static int f_ms(bvm *vm)
{
    LOCK();
    int64_t t = gclock_us();
    UNLOCK();
    be_pushint(vm, (bint)(t / 1000));
    be_return(vm);
}

static int f_count(bvm *vm)
{
    be_pushint(vm, s_cfg.nwords);
    be_return(vm);
}

static int word_at(bvm *vm, int which)
{
    int i = argi(vm, 1, -1);
    if (i < 0 || i >= s_cfg.nwords) be_raise(vm, "index_error", "tu ngoai khoang");
    const fd_berry_word_t *w = &s_cfg.words[i];
    const char *t = which == 0 ? w->en : which == 1 ? w->vi : w->ipa;
    be_pushstring(vm, t ? t : "");
    be_return(vm);
}
static int f_en(bvm *vm) { return word_at(vm, 0); }
static int f_vi(bvm *vm) { return word_at(vm, 1); }
static int f_ipa(bvm *vm) { return word_at(vm, 2); }   /* 4.9.6: phien am IPA tu kho tu may ("" neu khong co) */

/* 4.9.6 fd.sub(id, chu): dong phu nho (Noto 14, ve duoc IPA) duoi chu chinh, "" = an */
static int f_sub(bvm *vm)
{
    int id = arg_id(vm);
    const char *t = be_top(vm) >= 2 ? be_tostring(vm, 2) : "";
    LOCK();
    fd_berry_obj_t *o = touch(id);
    ucpy(o->sub, sizeof(o->sub), t);
    UNLOCK();
    be_return_nil(vm);
}

/* fd.style(id, vien_px, mau_vien, bo_goc, dem) */
static int f_style(bvm *vm)
{
    int id = arg_id(vm);
    int bw = argi(vm, 2, 0), bc = argi(vm, 3, 0xCBD5E1), r = argi(vm, 4, 10), pad = argi(vm, 5, 4);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    o->bw = (uint8_t)clampv(bw, 0, 8);
    o->bc = bc & 0xFFFFFF;
    o->radius = (uint8_t)clampv(r, 0, 255);   /* 5.1.0: 255 = tron */
    o->pad = (uint8_t)clampv(pad, 0, 20);
    UNLOCK();
    be_return_nil(vm);
}

/* fd.move(id, x, y, ms) */
static int f_move(bvm *vm)
{
    int id = arg_id(vm);
    int x = clampv(argi(vm, 2, 0), -1000, 2000), y = clampv(argi(vm, 3, 0), -1000, 2000);
    int ms = clampv(argi(vm, 4, 0), 0, 600000);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    if (s_anim[id].on) anim_step();   /* lay vi tri hien tai */
    anim_t *a = &s_anim[id];
    a->x0 = o->x;
    a->y0 = o->y;
    a->x1 = (int16_t)x;
    a->y1 = (int16_t)y;
    a->dur = ms;
    a->t0 = gclock_us();
    a->on = true;
    s_arrived &= ~(1u << id);
    UNLOCK();
    be_return_nil(vm);
}

static int f_stop(bvm *vm)
{
    int id = arg_id(vm);
    LOCK();
    if (s_anim[id].on) {
        anim_step();
        s_anim[id].on = false;
    }
    s_arrived &= ~(1u << id);
    UNLOCK();
    be_return_nil(vm);
}

/* fd.measure(chu, font, rong_toi_da) -> [w, h] */
static int f_measure(bvm *vm)
{
    const char *t = be_top(vm) >= 1 ? be_tostring(vm, 1) : "";
    int font = argi(vm, 2, 18), maxw = clampv(argi(vm, 3, 200), 10, 640);
    int w = 0, h = 0;
    if (!s_cfg.measure || !s_cfg.measure(t ? t : "", font, maxw, &w, &h)) {
        /* khong khoa duoc LVGL: uoc luong (chu trung binh ~0.55 co chu) */
        int px = font >= 32 ? 32 : font >= 18 ? 18 : 14, lh = px + px / 3;
        int tw = (int)strlen(t ? t : "") * px * 55 / 100;
        int lines = tw / maxw + 1;
        w = tw > maxw ? maxw : tw;
        h = lines * lh;
    }
    be_newobject(vm, "list");
    be_pushint(vm, w);
    be_data_push(vm, -2);
    be_pop(vm, 1);
    be_pushint(vm, h);
    be_data_push(vm, -2);
    be_pop(vm, 1);
    be_pop(vm, 1);
    be_return(vm);
}

/* 5.1.0 fd.pos(id) -> [x, y] vi tri hien tai (ke ca dang fd.move), de no hat dung cho */
static int f_pos(bvm *vm)
{
    int id = arg_id(vm);
    LOCK();
    anim_step();
    int x = s_obj[id].x, y = s_obj[id].y;
    UNLOCK();
    be_newobject(vm, "list");
    be_pushint(vm, x);
    be_data_push(vm, -2);
    be_pop(vm, 1);
    be_pushint(vm, y);
    be_data_push(vm, -2);
    be_pop(vm, 1);
    be_pop(vm, 1);
    be_return(vm);
}

/* ---------- 5.1.0 do hoa ---------- */
/* fd.grad(id, mau2[, huong]) */
static int f_grad(bvm *vm)
{
    int id = arg_id(vm);
    int c2 = argi(vm, 2, 0xFFFFFF), dir = argi(vm, 3, 1);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    o->bg2 = c2 & 0xFFFFFF;
    o->grad = (uint8_t)clampv(dir, 0, 2);
    UNLOCK();
    be_return_nil(vm);
}

/* fd.opa(id, 0..255) */
static int f_opa(bvm *vm)
{
    int id = arg_id(vm);
    int a = clampv(argi(vm, 2, 255), 0, 255);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    o->opa = (uint8_t)a;
    if (!s_fx[id].kind) o->opa_now = (uint8_t)a;
    UNLOCK();
    be_return_nil(vm);
}

/* fd.fx(id, kieu, ms) */
static int f_fx(bvm *vm)
{
    int id = arg_id(vm);
    int k = clampv(argi(vm, 2, 0), 0, 5), ms = clampv(argi(vm, 3, 300), 0, 10000);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    fx_reset(id);
    if (k) {
        s_fx[id].kind = (uint8_t)k;
        s_fx[id].t0 = gclock_us();
        s_fx[id].dur = ms;
        if (k == 3) { o->vis = true; o->opa_now = 0; }
    }
    UNLOCK();
    be_return_nil(vm);
}

/* fd.sway(id, bien_do, chu_ky_ms) */
static int f_sway(bvm *vm)
{
    int id = arg_id(vm);
    int amp = clampv(argi(vm, 2, 0), 0, 40), per = clampv(argi(vm, 3, 2000), 200, 20000);
    LOCK();
    fd_berry_obj_t *o = touch(id);
    s_sway[id].amp = (int16_t)amp;
    s_sway[id].period = per;
    if (!amp) o->ox = 0;
    UNLOCK();
    be_return_nil(vm);
}

/* fd.deco(id, mat_na) */
static int f_deco(bvm *vm)
{
    int id = arg_id(vm);
    int m = clampv(argi(vm, 2, 0), 0, 7);
    LOCK();
    touch(id)->deco = (uint8_t)m;
    UNLOCK();
    be_return_nil(vm);
}

/* fd.burst(x, y, mau[, so_hat]) */
static int f_burst(bvm *vm)
{
    int x = clampv(argi(vm, 1, 0), -100, 1000), y = clampv(argi(vm, 2, 0), -100, 1000);
    int c = argi(vm, 3, 0xF59E0B), n = clampv(argi(vm, 4, 10), 1, 16);
    LOCK();
    if (s_misc.nburst < FD_BERRY_BURST_Q) {
        fd_berry_burst_t *b = &s_misc.burst[s_misc.nburst++];
        b->x = (int16_t)x;
        b->y = (int16_t)y;
        b->color = c & 0xFFFFFF;
        b->n = (uint8_t)n;
    }
    UNLOCK();
    be_return_nil(vm);
}

/* fd.opt(ten) */
static int f_opt(bvm *vm)
{
    const char *k = be_top(vm) >= 1 ? be_tostring(vm, 1) : "";
    const fd_berry_opts_t *o = &s_cfg.opts;
    static const struct { const char *k; size_t off; } T[] = {
        {"dir", offsetof(fd_berry_opts_t, dir)}, {"spd", offsetof(fd_berry_opts_t, spd)},
        {"rep", offsetof(fd_berry_opts_t, rep)}, {"fall_ms", offsetof(fd_berry_opts_t, fall_ms)},
        {"min_ms", offsetof(fd_berry_opts_t, min_ms)}, {"every", offsetof(fd_berry_opts_t, every)},
        {"ramp_pct", offsetof(fd_berry_opts_t, ramp_pct)}, {"lives", offsetof(fd_berry_opts_t, lives)},
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        if (k && !strcmp(k, T[i].k)) { be_pushint(vm, *(const int *)((const char *)o + T[i].off)); be_return(vm); }
    be_return_nil(vm);
}

/* ---------- 5.4.0 tinh nang tai ve ---------- */
static void cmd_push(uint8_t type, const char *arg)
{
    LOCK();
    if (s_cmd_n < CMD_Q) {
        fd_berry_cmd_t *c = &s_cmd[(s_cmd_h + s_cmd_n) % CMD_Q];
        c->type = type;
        ucpy(c->arg, sizeof(c->arg), arg ? arg : "");
        s_cmd_n++;
    } else {
        ESP_LOGW(TAG, "hang lenh day");
    }
    UNLOCK();
}

static const char *args(bvm *vm, int i)
{
    if (be_top(vm) < i) return "";
    const char *t = be_tostring(vm, i);
    return t ? t : "";
}

/* fd.align(id, 0 giua | 1 trai | 2 trai mot dong) */
static int f_align(bvm *vm)
{
    int id = arg_id(vm);
    int a = argi(vm, 2, 0);
    LOCK();
    touch(id)->align = (uint8_t)clampv(a, 0, 2);
    UNLOCK();
    be_return_nil(vm);
}

static int f_say_long(bvm *vm)
{
    const char *t = args(vm, 1);
    int spd = clampv(argi(vm, 2, 100), 50, 200);
    char arg[16];
    snprintf(arg, sizeof(arg), "%d", spd);
    if (!s_long) be_return_nil(vm);
    LOCK();
    ucpy(s_long, FD_BERRY_LONG_MAX, t);
    s_long_pend = true;
    UNLOCK();
    cmd_push(FD_BERRY_CMD_SAY_LONG, arg);
    be_return_nil(vm);
}

static int f_say_stop(bvm *vm)
{
    LOCK();
    s_long_pend = false;
    UNLOCK();
    cmd_push(FD_BERRY_CMD_SAY_STOP, NULL);
    be_return_nil(vm);
}

static int f_busy(bvm *vm)
{
    be_pushbool(vm, s_busy || s_long_pend);
    be_return(vm);
}

static int f_pass_count(bvm *vm)
{
    const fd_berry_host_t *h = s_cfg.host;
    be_pushint(vm, h && h->pass_count ? h->pass_count() : 0);
    be_return(vm);
}

static int f_pass_at(bvm *vm)
{
    const fd_berry_host_t *h = s_cfg.host;
    char t[FD_BERRY_TXT];
    if (!h || !h->pass_at || !h->pass_at(argi(vm, 1, -1), t, sizeof(t))) be_return_nil(vm);
    be_pushstring(vm, t);
    be_return(vm);
}

/* fd.pass_load(i) -> [[cau, nghia, ipa], ...] (rong neu khong co) */
static int f_pass_load(bvm *vm)
{
    const fd_berry_host_t *h = s_cfg.host;
    int n = (h && h->pass_load && s_lines) ? h->pass_load(argi(vm, 1, -1), s_lines, FD_BERRY_LINES) : 0;
    if (n < 0) n = 0;
    if (n > FD_BERRY_LINES) n = FD_BERRY_LINES;
    be_newobject(vm, "list");
    for (int i = 0; i < n; i++) {
        be_newobject(vm, "list");
        const char *f[3] = {s_lines[i].text, s_lines[i].vi, s_lines[i].ipa};
        for (int k = 0; k < 3; k++) {
            be_pushstring(vm, f[k]);
            be_data_push(vm, -2);
            be_pop(vm, 1);
        }
        be_pop(vm, 1);
        be_data_push(vm, -2);
        be_pop(vm, 1);
    }
    be_pop(vm, 1);
    be_return(vm);
}

static int pref_idx(const char *k)
{
    for (int i = 0; i < PREF_N; i++) if (k && !strcmp(k, PREF_K[i])) return i;
    return -1;
}

static int f_pref_get(bvm *vm)
{
    int i = pref_idx(args(vm, 1));
    if (i < 0) be_return_nil(vm);
    be_pushint(vm, s_pref[i]);
    be_return(vm);
}

static int f_pref_set(bvm *vm)
{
    int i = pref_idx(args(vm, 1));
    if (i < 0) { be_pushbool(vm, false); be_return(vm); }
    int v = clampv(argi(vm, 2, 0), 0, 255);
    LOCK();
    s_pref[i] = (uint8_t)v;
    s_pref_dirty |= 1u << i;
    UNLOCK();
    be_pushbool(vm, true);
    be_return(vm);
}

static void prefs_load(void)
{
    for (int i = 0; i < PREF_N; i++) s_pref[i] = PREF_DEF[i];
    s_pref_dirty = 0;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READONLY, &h) != ESP_OK) return;
    for (int i = 0; i < PREF_N; i++) nvs_get_u8(h, PREF_K[i], &s_pref[i]);
    nvs_close(h);
}

static int f_session_begin(bvm *vm) { cmd_push(FD_BERRY_CMD_SESS_BEGIN, args(vm, 1)); be_return_nil(vm); }
static int f_event(bvm *vm) { cmd_push(FD_BERRY_CMD_EVENT, args(vm, 1)); be_return_nil(vm); }
static int f_session_end(bvm *vm) { cmd_push(FD_BERRY_CMD_SESS_END, NULL); be_return_nil(vm); }

static int f_tr(bvm *vm)
{
    const char *k = args(vm, 1);
    const fd_berry_host_t *h = s_cfg.host;
    const char *t = (h && h->tr) ? h->tr(k) : NULL;
    be_pushstring(vm, t ? t : k);
    be_return(vm);
}

static int f_roll_box(bvm *vm)
{
    LOCK();
    s_rst.x = (int16_t)clampv(argi(vm, 1, 0), -1000, 2000);
    s_rst.y = (int16_t)clampv(argi(vm, 2, 0), -1000, 2000);
    s_rst.w = (int16_t)clampv(argi(vm, 3, 0), 0, 640);
    s_rst.h = (int16_t)clampv(argi(vm, 4, 0), 0, 640);
    s_rst.ver++;
    UNLOCK();
    be_return_nil(vm);
}

static int f_roll_clear(bvm *vm)
{
    LOCK();
    s_rst.n = 0;
    s_nmark = 0;
    s_rst.ver++;
    UNLOCK();
    be_return_nil(vm);
}

/* fd.roll_add(chu, font, mau[, opa]) */
static int f_roll_add(bvm *vm)
{
    const char *t = args(vm, 1);
    int f = argi(vm, 2, 18), c = argi(vm, 3, 0x111827), o = argi(vm, 4, 255);
    bool ok = false;
    LOCK();
    if (s_roll && s_rst.n < FD_BERRY_ROLL_MAX) {
        fd_berry_roll_item_t *it = &s_roll[s_rst.n++];
        ucpy(it->txt, sizeof(it->txt), t);
        it->font = (uint8_t)(f >= 32 ? 32 : f >= 24 ? 24 : f >= 18 ? 18 : f >= 14 ? 14 : 12);
        it->color = c & 0xFFFFFF;
        it->opa = (uint8_t)clampv(o, 0, 255);
        s_rst.ver++;
        ok = true;
    }
    UNLOCK();
    be_pushbool(vm, ok);
    be_return(vm);
}

static int f_roll_run(bvm *vm)
{
    LOCK();
    s_rst.est_ms = clampv(argi(vm, 1, 8000), 0, 600000);
    s_rst.silent = be_top(vm) >= 2 && be_tobool(vm, 2);
    s_rst.run_seq++;
    UNLOCK();
    be_return_nil(vm);
}

/* fd.roll_focus(i, n[, est_ms[, silent]]) */
static int f_roll_focus(bvm *vm)
{
    LOCK();
    s_rst.focus_i = (int16_t)clampv(argi(vm, 1, 0), 0, FD_BERRY_ROLL_MAX - 1);
    s_rst.focus_n = (int16_t)clampv(argi(vm, 2, 1), 1, FD_BERRY_ROLL_MAX);
    s_rst.focus_est = be_top(vm) >= 3 ? clampv(argi(vm, 3, 0), 0, 600000) : 0;
    s_rst.silent = be_top(vm) >= 4 && be_tobool(vm, 4);
    s_rst.focus_seq++;
    UNLOCK();
    be_return_nil(vm);
}

/* fd.roll_mark(i, n, est_ms) */
static int f_roll_mark(bvm *vm)
{
    LOCK();
    if (s_nmark < FD_BERRY_MARK_MAX) {
        fd_berry_roll_mark_t *m = &s_marks[s_nmark++];
        m->i = (int16_t)clampv(argi(vm, 1, 0), 0, FD_BERRY_ROLL_MAX - 1);
        m->n = (int16_t)clampv(argi(vm, 2, 1), 1, FD_BERRY_ROLL_MAX);
        m->est = clampv(argi(vm, 3, 0), 0, 600000);
    }
    UNLOCK();
    be_return_nil(vm);
}

static int f_roll_y(bvm *vm) { be_pushint(vm, s_roll_y); be_return(vm); }

static int f_hold_back(bvm *vm)
{
    s_hold_back = be_top(vm) < 1 || be_tobool(vm, 1);
    be_return_nil(vm);
}

bool fd_berry_cmd_take(fd_berry_cmd_t *out)
{
    if (!s_mx || !out) return false;
    LOCK();
    bool ok = s_cmd_n > 0;
    if (ok) {
        *out = s_cmd[s_cmd_h];
        s_cmd_h = (s_cmd_h + 1) % CMD_Q;
        s_cmd_n--;
    }
    UNLOCK();
    return ok;
}

size_t fd_berry_long_text(char *out, size_t cap)
{
    if (!s_mx || !out || !cap) return 0;
    LOCK();
    ucpy(out, cap, s_long_pend && s_long ? s_long : "");
    s_long_pend = false;
    UNLOCK();
    return strlen(out);
}

void fd_berry_set_busy(bool busy) { s_busy = busy; }

void fd_berry_pref_flush(void)
{
    if (!s_mx) return;
    LOCK();
    uint32_t d = s_pref_dirty;
    uint8_t v[PREF_N];
    memcpy(v, s_pref, sizeof(v));
    s_pref_dirty = 0;
    UNLOCK();
    if (!d) return;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    for (int i = 0; i < PREF_N; i++) if (d & (1u << i)) nvs_set_u8(h, PREF_K[i], v[i]);
    nvs_commit(h);
    nvs_close(h);
}

void fd_berry_roll_get(fd_berry_roll_t *out)
{
    if (!out) return;
    if (!s_mx) { memset(out, 0, sizeof(*out)); return; }
    LOCK();
    *out = s_rst;
    UNLOCK();
}

int fd_berry_roll_items(fd_berry_roll_item_t *out, int cap)
{
    if (!s_mx || !out || !s_roll) return 0;
    LOCK();
    int n = s_rst.n < cap ? s_rst.n : cap;
    if (n > 0) memcpy(out, s_roll, sizeof(*out) * (size_t)n);
    UNLOCK();
    return n;
}

int fd_berry_roll_marks(fd_berry_roll_mark_t *out, int cap)
{
    if (!s_mx || !out || cap <= 0) return 0;
    LOCK();
    int n = s_nmark < cap ? s_nmark : cap;
    if (n > 0) memcpy(out, s_marks, sizeof(*out) * (size_t)n);
    UNLOCK();
    return n;
}

void fd_berry_roll_set_y(int y) { s_roll_y = y; }
bool fd_berry_hold_back(void) { return s_running && s_hold_back; }

static void reg_module(bvm *vm)
{
    static const struct { const char *name; bntvfunc f; } F[] = {
        {"text", f_text}, {"box", f_box}, {"color", f_color}, {"font", f_font}, {"show", f_show}, {"clear", f_clear},
        {"hud", f_hud}, {"sound", f_sound}, {"say", f_say}, {"over", f_over}, {"rand", f_rand}, {"ms", f_ms},
        {"count", f_count}, {"en", f_en}, {"vi", f_vi}, {"style", f_style}, {"move", f_move}, {"stop", f_stop},
        {"measure", f_measure}, {"opt", f_opt}, {"ipa", f_ipa}, {"sub", f_sub},
        {"grad", f_grad}, {"opa", f_opa}, {"fx", f_fx}, {"sway", f_sway}, {"deco", f_deco}, {"burst", f_burst}, {"pos", f_pos},
        {"align", f_align}, {"say_long", f_say_long}, {"say_stop", f_say_stop}, {"busy", f_busy},
        {"pass_count", f_pass_count}, {"pass_at", f_pass_at}, {"pass_load", f_pass_load},
        {"pref_get", f_pref_get}, {"pref_set", f_pref_set}, {"session_begin", f_session_begin}, {"event", f_event},
        {"session_end", f_session_end}, {"tr", f_tr}, {"roll_box", f_roll_box}, {"roll_clear", f_roll_clear},
        {"roll_add", f_roll_add}, {"roll_run", f_roll_run}, {"roll_focus", f_roll_focus}, {"roll_mark", f_roll_mark}, {"roll_y", f_roll_y}, {"hold_back", f_hold_back},
    };
    be_newmodule(vm);
    for (size_t i = 0; i < sizeof(F) / sizeof(F[0]); i++) {
        be_pushntvfunction(vm, F[i].f);
        be_setmember(vm, -2, F[i].name);
        be_pop(vm, 1);
    }
    be_pushint(vm, s_cfg.w);
    be_setmember(vm, -2, "W");
    be_pop(vm, 1);
    be_pushint(vm, s_cfg.h);
    be_setmember(vm, -2, "H");
    be_pop(vm, 1);
    be_pushstring(vm, s_cfg.side ? s_cfg.side : "bottom");
    be_setmember(vm, -2, "side");
    be_pop(vm, 1);
    be_setglobal(vm, "fd");
    be_pop(vm, 1);
}

/* ---------- gioi han thoi gian: heartbeat moi 2^13 lenh ---------- */
static void obs_hook(bvm *vm, int event, ...)
{
    if (event == BE_OBS_VM_HEARTBEAT) {
        if (s_stop) be_raise(vm, "stop", "dung");
        if (esp_timer_get_time() > s_deadline) be_raise(vm, "timeout_error", "script chay qua lau");
    }
}

/* Goi ham toan cuc name voi argc so nguyen. false = loi (da ghi err). */
static bool call_fn(bvm *vm, const char *name, int argc, int a0, int64_t budget_ms)
{
    if (!be_getglobal(vm, name)) { be_pop(vm, 1); return true; }   /* khong dinh nghia: bo qua */
    if (!be_isfunction(vm, -1)) { be_pop(vm, 1); return true; }
    if (argc > 0) be_pushint(vm, a0);
    int64_t c0 = esp_timer_get_time();
    s_deadline = c0 + budget_ms * 1000;
    int r = be_pcall(vm, argc);
    uint32_t us = (uint32_t)(esp_timer_get_time() - c0);
    s_calls++;
    s_sum_us += us;
    if (us > s_max_us) s_max_us = us;
    if (r != BE_OK) {
        if (!s_stop || s_misc.state == FD_BERRY_RUNNING) {
            bool exc = (r == BE_EXCEPTION && be_top(vm) >= 2);
            const char *ex = exc ? be_tostring(vm, -2) : (r == BE_MALLOC_FAIL ? "memory_error" : "error");
            const char *msg = exc ? be_tostring(vm, -1) : "";
            if (strcmp(ex ? ex : "", "stop") != 0) {
                ESP_LOGW(TAG, "%s(): %s: %s", name, ex ? ex : "?", msg ? msg : "");
                set_err(ex, msg);
            }
        }
        be_pop(vm, be_top(vm));
        return false;
    }
    be_pop(vm, be_top(vm));
    return true;
}

static void run_game(void)
{
    s_t0 = esp_timer_get_time();
    s_calls = 0;
    s_sum_us = 0;
    s_max_us = 0;
    s_mem_peak = s_mem;
    bvm *vm = be_vm_new();
    if (!vm) { set_err("memory_error", "khong tao duoc may ao"); goto out; }
    be_set_obs_hook(vm, obs_hook);
    reg_module(vm);
    s_deadline = esp_timer_get_time() + FD_BERRY_INIT_MS * 1000;
    int r = be_loadbuffer(vm, "game", s_cfg.script, s_cfg.len);
    if (r == BE_OK) r = be_pcall(vm, 0);
    if (r != BE_OK) {
        bool exc = (r == BE_EXCEPTION && be_top(vm) >= 2);
        const char *ex = exc ? be_tostring(vm, -2) : (r == BE_MALLOC_FAIL ? "memory_error" : "error");
        const char *msg = exc ? be_tostring(vm, -1) : "";
        ESP_LOGW(TAG, "nap script: %s: %s", ex ? ex : "?", msg ? msg : "");
        set_err(ex, msg);
        goto out;
    }
    be_pop(vm, be_top(vm));
    free(s_cfg.script);   /* da bien dich, giai phong ma nguon som */
    s_cfg.script = NULL;
    if (s_cfg.need_init) {   /* 5.4.0: goi tinh nang phai co init(), khong thi man hinh dung ban C */
        bool has = be_getglobal(vm, "init") && be_isfunction(vm, -1);
        be_pop(vm, 1);
        if (!has) { set_err("no_init", NULL); goto out; }
    }
    if (!call_fn(vm, "init", 0, 0, FD_BERRY_INIT_MS)) goto out;
    int64_t last = esp_timer_get_time();
    TickType_t wake = xTaskGetTickCount();
    while (!s_stop) {
        ev_t ev;
        while (!s_stop && xQueueReceive(s_q, &ev, 0) == pdTRUE) {
            if (s_paused) continue;
            const char *fn = ev.type == FD_BERRY_EV_BUTTON ? "button" : ev.type == FD_BERRY_EV_BACK ? "back" : "tap";
            if (!call_fn(vm, fn, ev.type == FD_BERRY_EV_BACK ? 0 : 1, ev.arg, FD_BERRY_FRAME_MS)) goto out;
        }
        /* hoat hinh toi dich -> arrive(id) */
        if (!s_paused) {
            LOCK();
            anim_step();
            uint32_t arr = s_arrived;
            s_arrived = 0;
            UNLOCK();
            for (int i = 0; i < FD_BERRY_OBJ_MAX && arr && !s_stop; i++)
                if (arr & (1u << i)) {
                    arr &= ~(1u << i);
                    if (!call_fn(vm, "arrive", 1, i, FD_BERRY_FRAME_MS)) goto out;
                }
        }
        int64_t now = esp_timer_get_time();
        int dt = (int)((now - last) / 1000);
        last = now;
        if (dt > 100) dt = 100;
        if (!s_paused && !s_stop && !call_fn(vm, "tick", 1, dt, FD_BERRY_FRAME_MS)) goto out;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FD_BERRY_TICK_MS));
        if (xTaskGetTickCount() - wake > pdMS_TO_TICKS(200)) wake = xTaskGetTickCount();
    }
out:
    if (vm) be_vm_delete(vm);
    free(s_cfg.script);
    s_cfg.script = NULL;
    ESP_LOGI(TAG, "dung: %u lan goi script, trung binh %u us, lau nhat %u us, bo nho dinh %u B (con %u B)",
             (unsigned)s_calls, (unsigned)(s_calls ? s_sum_us / s_calls : 0), (unsigned)s_max_us,
             (unsigned)s_mem_peak, (unsigned)s_mem);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_game();
        s_running = false;
        xSemaphoreGive(s_done);
    }
}

bool fd_berry_start(const fd_berry_cfg_t *cfg)
{
    if (!cfg || !cfg->script || cfg->len == 0) return false;
    if (!s_mx) {
        s_mx = xSemaphoreCreateMutex();
        s_done = xSemaphoreCreateBinary();
        s_q = xQueueCreate(16, sizeof(ev_t));
        if (!s_mx || !s_done || !s_q) return false;
    }
    if (s_running) return false;
    if (cfg->host) {   /* 5.4.0 bo dem tinh nang, cap mot lan, giu lai */
        if (!s_long) s_long = heap_caps_calloc(1, FD_BERRY_LONG_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_roll) s_roll = heap_caps_calloc(FD_BERRY_ROLL_MAX, sizeof(fd_berry_roll_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_lines) s_lines = heap_caps_calloc(FD_BERRY_LINES, sizeof(fd_berry_line_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_long || !s_roll || !s_lines) return false;
        /* Doc NVS o task goi (LVGL, stack noi bo). Task fd_berry co stack PSRAM: doc flash o do
           tat cache va assert (5.4.0 khoi dong lai khi mo Nhai lai). Native trong VM khong duoc
           cham flash, NVS, LittleFS: chi doc RAM hoac xep hang cho task LVGL. */
        prefs_load();
    }
    if (!s_task &&
        xTaskCreatePinnedToCoreWithCaps(worker, "fd_berry", STACK_BYTES, NULL, 2, &s_task, tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        return false;
    }
    LOCK();
    memset(s_obj, 0, sizeof(s_obj));
    s_dirty = 0;
    memset(&s_misc, 0, sizeof(s_misc));
    memset(s_anim, 0, sizeof(s_anim));
    memset(s_fx, 0, sizeof(s_fx));
    memset(s_sway, 0, sizeof(s_sway));
    s_arrived = 0;
    s_pause_acc = 0;
    s_pause_at = 0;
    s_cmd_h = s_cmd_n = 0;
    s_long_pend = false;
    s_busy = false;
    s_hold_back = false;
    memset(&s_rst, 0, sizeof(s_rst));
    memset(s_marks, 0, sizeof(s_marks));
    s_nmark = 0;
    s_roll_y = 0;
    s_misc.over_right = -1;
    s_misc.state = FD_BERRY_RUNNING;
    s_misc.sound = -1;
    s_misc.lives = -1;
    s_cfg = *cfg;
    UNLOCK();
    xQueueReset(s_q);
    xSemaphoreTake(s_done, 0);
    s_stop = false;
    s_paused = false;
    s_running = true;
    xTaskNotifyGive(s_task);
    return true;
}

void fd_berry_stop(void)
{
    if (!s_running) return;
    s_stop = true;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) != pdTRUE) ESP_LOGW(TAG, "VM chua dung kip");
}

void fd_berry_event(fd_berry_ev_t type, int arg)
{
    if (!s_running || !s_q) return;
    ev_t ev = {(uint8_t)type, (int16_t)arg};
    xQueueSend(s_q, &ev, 0);
}

void fd_berry_pause(bool paused)
{
    if (!s_mx || paused == s_paused) return;
    LOCK();
    int64_t now = esp_timer_get_time();
    if (paused) s_pause_at = now;
    else if (s_pause_at) { s_pause_acc += now - s_pause_at; s_pause_at = 0; }
    s_paused = paused;
    UNLOCK();
}

void fd_berry_perf(fd_berry_perf_t *out)
{
    if (!out) return;
    out->calls = s_calls;
    out->max_us = s_max_us;
    out->avg_us = s_calls ? (uint32_t)(s_sum_us / s_calls) : 0;
    out->peak_mem = s_mem_peak;
}

uint32_t fd_berry_sync(fd_berry_obj_t *out, fd_berry_misc_t *misc)
{
    if (!s_mx) {
        if (misc) memset(misc, 0, sizeof(*misc));
        return 0;
    }
    LOCK();
    if (!s_paused && s_running) anim_step();   /* vi tri muot theo tung khung man hinh */
    uint32_t d = s_dirty;
    for (int i = 0; i < FD_BERRY_OBJ_MAX; i++)
        if (d & (1u << i)) out[i] = s_obj[i];
    s_dirty = 0;
    if (misc) *misc = s_misc;
    s_misc.hud_dirty = false;
    s_misc.sound = -1;
    s_misc.say[0] = 0;
    s_misc.nburst = 0;
    UNLOCK();
    return d;
}
