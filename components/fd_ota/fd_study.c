/* Hoc tap tu API: xem fd_study.h. KHONG log device_token / Authorization / body. */
#include "fd_study.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fd_deck.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fd_study";

#define RESP_CAP (24 * 1024)   /* 20 cau x ~600 B + du phong; cat bot neu dai hon -> JSON loi -> ERR_FORMAT */

#define SEG_MAX   3   /* phien nap cung luc (dang lam + nap truoc) */
#define LOOKAHEAD 4   /* con < 4 cau chua lam da nap -> nap phien tiep */

typedef struct {
    bool used;              /* slot co phien */
    bool from_cache;        /* lay tu /data/study_cache (da danh dau .usd) */
    int start;              /* chi so cau toan bai cua cau dau phien */
    int answered;
    int8_t ch[FD_STUDY_MAX];
    fd_study_session_t s;
} seg_t;

static SemaphoreHandle_t s_mx;
static fd_study_status_t s_st;
static seg_t *s_seg;              /* SEG_MAX, PSRAM */
static volatile bool s_busy;
static volatile uint32_t s_gen;   /* tang khi cancel/start/finish: ket qua nap cu bi bo */
static char s_dir[8];
static bool s_local;              /* chi cache */
static int s_count;               /* 10/20, 0 = vo tan */
static int s_loaded;              /* tong cau da nap (chi so cau ke tiep) */
static int s_cur;                 /* cau dang lam */
static bool s_dry;                /* lan nap cuoi khong co cau (moi nguon loi/het) */

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static bool init(void)
{
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_seg) {
        s_seg = heap_caps_calloc(SEG_MAX, sizeof(seg_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_seg) s_seg = calloc(SEG_MAX, sizeof(seg_t));
    }
    return s_mx && s_seg;
}

static void *rx_alloc(void)
{
    void *p = heap_caps_malloc(RESP_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(RESP_CAP);
}

/* sao chep chuoi UTF-8 an toan (khong cat giua ky tu nhieu byte) */
static void ucpy(char *out, size_t cap, const char *in)
{
    size_t n = in ? strlen(in) : 0;
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)in[n] & 0xC0) == 0x80) n--;   /* lui ve dau ky tu */
    }
    if (n) memcpy(out, in, n);
    out[n] = 0;
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

static int key_idx(const char *k)
{
    if (!k || !k[0] || k[1]) return -1;
    char c = k[0];
    if (c >= 'a' && c <= 'd') c -= 32;
    return (c >= 'A' && c <= 'D') ? c - 'A' : -1;
}

/* id cau: chuoi hoac so */
static bool qid(const cJSON *o, char *out, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "id");
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) { ucpy(out, cap, v->valuestring); return true; }
    if (cJSON_IsNumber(v)) { snprintf(out, cap, "%lld", (long long)v->valuedouble); return true; }
    return false;
}

fd_study_err_t fd_study_parse_start(const char *body, fd_study_session_t *s)
{
    cJSON *r = cJSON_Parse(body);
    if (!r) return FD_STUDY_ERR_FORMAT;
    fd_study_err_t err = FD_STUDY_ERR_FORMAT;
    memset(s, 0, sizeof(*s));
    const char *sid = jstr(r, "session_id");
    const cJSON *qs = cJSON_GetObjectItemCaseSensitive(r, "questions");
    if (!sid || !sid[0] || strlen(sid) >= FD_STUDY_ID_LEN || !cJSON_IsArray(qs)) goto out;
    strcpy(s->session_id, sid);
    const cJSON *q;
    cJSON_ArrayForEach(q, qs) {
        if (s->n >= FD_STUDY_MAX) break;
        fd_study_q_t *d = &s->q[s->n];
        if (!cJSON_IsObject(q) || !qid(q, d->id, sizeof(d->id))) goto out;
        const char *pr = jstr(q, "prompt");
        if (!pr) goto out;
        ucpy(d->prompt, sizeof(d->prompt), pr);
        ucpy(d->ipa, sizeof(d->ipa), jstr(q, "ipa"));
        const cJSON *ch = cJSON_GetObjectItemCaseSensitive(q, "choices");
        if (!cJSON_IsArray(ch)) goto out;
        unsigned seen = 0;
        const cJSON *c;
        cJSON_ArrayForEach(c, ch) {
            int k = key_idx(jstr(c, "key"));
            const char *t = jstr(c, "text");
            if (k < 0 || !t || (seen & (1u << k))) goto out;
            seen |= 1u << k;
            ucpy(d->choice[k], sizeof(d->choice[k]), t);
        }
        d->answer = (int8_t)key_idx(jstr(q, "answer"));
        if (seen != 0xF || d->answer < 0) goto out;
        s->n++;
    }
    if (s->n > 0) err = FD_STUDY_ERR_NONE;
out:
    cJSON_Delete(r);
    if (err != FD_STUDY_ERR_NONE) memset(s, 0, sizeof(*s));
    return err;
}

static fd_study_err_t http_err(int code)
{
    if (code == 401) return FD_STUDY_ERR_UNPAIRED;   /* fd_ota da xoa token + ten */
    if (code < 0) return FD_STUDY_ERR_NET;
    return FD_STUDY_ERR_SERVER;
}

/* nap 1 phien: cache chua dung -> study/start (online) -> cache bat ky (hoc vong lai). Goi tu task. */
static fd_study_err_t fetch_one(const char *dir, bool local, fd_study_session_t *out, bool *from_cache, int *http)
{
    bool vi = !strcmp(dir, "vi-en");
    *http = 0;
    *from_cache = true;
    if (fd_deck_has_fresh(vi) && fd_deck_build_session(out, vi, true) == FD_STUDY_ERR_NONE) return FD_STUDY_ERR_NONE;
    fd_study_err_t err = FD_STUDY_ERR_NO_DECK;
    if (!local) {
        if (!fd_ota_is_paired()) err = FD_STUDY_ERR_UNPAIRED;
        else if (!fd_net_is_connected()) err = FD_STUDY_ERR_NO_WIFI;
        else {
            char id[16], *body = NULL, *rx = rx_alloc();
            fd_ota_get_device_id(id, sizeof(id));
            cJSON *b = cJSON_CreateObject();
            if (b) {
                cJSON_AddStringToObject(b, "device_id", id);
                cJSON_AddStringToObject(b, "direction", dir);
                cJSON_AddNumberToObject(b, "count", 10);
                cJSON_AddStringToObject(b, "source", "deck");
                body = cJSON_PrintUnformatted(b);
                cJSON_Delete(b);
            }
            if (!body || !rx) err = FD_STUDY_ERR_NOMEM;
            else {
                int code = fd_ota_api_post_ex("/api/study/start", body, rx, RESP_CAP, false);   /* tuong tac: khong cho 429 */
                *http = code;
                err = code == 200 ? fd_study_parse_start(rx, out) : http_err(code);
            }
            free(body);
            free(rx);
            if (!err) { *from_cache = false; return err; }
        }
    }
    if (fd_deck_ready() && fd_deck_build_session(out, vi, false) == FD_STUDY_ERR_NONE) return FD_STUDY_ERR_NONE;
    return err;
}

/* con can nap? (khoa) */
static bool need_more(void)
{
    if (s_count && s_loaded >= s_count) return false;
    if (s_loaded - s_cur >= LOOKAHEAD) return false;
    for (int i = 0; i < SEG_MAX; i++) if (!s_seg[i].used) return true;
    return false;
}

static void task(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    fd_study_session_t *tmp = heap_caps_calloc(1, sizeof(*tmp), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char dir[8];
    for (;;) {
        lock();
        bool go = gen == s_gen && need_more();
        bool local = s_local;
        strlcpy(dir, s_dir, sizeof(dir));
        unlock();
        if (!go || !tmp) break;
        int64_t t0 = esp_timer_get_time();
        bool fc;
        int http;
        fd_study_err_t err = fetch_one(dir, local, tmp, &fc, &http);
        ESP_LOGI(TAG, "nap phien: dir=%s %s -> err=%d http=%d n=%d (%d ms)", dir, err ? "-" : (fc ? "cache" : "online"), err, http,
                 err ? 0 : tmp->n, (int)((esp_timer_get_time() - t0) / 1000));
        lock();
        bool mine = gen == s_gen;
        if (mine) {
            s_st.http = http;
            if (err) {
                s_dry = true;
                if (s_st.state == FD_STUDY_LOADING) { s_st.err = err; s_st.state = FD_STUDY_ERROR; }
            } else {
                for (int i = 0; i < SEG_MAX; i++) {
                    if (s_seg[i].used) continue;
                    seg_t *g = &s_seg[i];
                    g->used = true;
                    g->from_cache = fc;
                    g->start = s_loaded;
                    g->answered = 0;
                    memset(g->ch, -1, sizeof(g->ch));
                    memcpy(&g->s, tmp, sizeof(*tmp));
                    s_loaded += tmp->n;
                    break;
                }
                s_dry = false;
                s_st.err = FD_STUDY_ERR_NONE;
                s_st.state = FD_STUDY_READY;
            }
        }
        unlock();
        if (!err && fc && mine) {
            fd_deck_cache_used(tmp->session_id);   /* da lay: khong lay lai trong bai nay (chua lam cau nao -> tra lai luc ket thuc) */
            fd_deck_refill();
        }
        if (err || !mine) break;
    }
    free(tmp);
    s_busy = false;
    vTaskDelete(NULL);
}

static void kick(void)
{
    lock();
    bool go = !s_busy && need_more();
    uint32_t gen = s_gen;
    if (go) s_busy = true;
    unlock();
    if (!go) return;
    if (xTaskCreatePinnedToCore(task, "fd_study", 8192, (void *)(uintptr_t)gen, 3, NULL, tskNO_AFFINITY) != pdPASS) {
        s_busy = false;
        lock();
        if (s_st.state == FD_STUDY_LOADING) { s_st.err = FD_STUDY_ERR_NOMEM; s_st.state = FD_STUDY_ERROR; }
        unlock();
    }
}

/* JSON /api/study/result: {device_id, session_id, answers:[{id, choice "A".."D" | ""}]}. */
static char *build_result(const seg_t *g, int *correct)
{
    char id[16];
    fd_ota_get_device_id(id, sizeof(id));
    int ok_n = 0;
    char *body = NULL;
    cJSON *b = cJSON_CreateObject();
    if (b) {
        cJSON_AddStringToObject(b, "device_id", id);
        cJSON_AddStringToObject(b, "session_id", g->s.session_id);
        cJSON *arr = cJSON_AddArrayToObject(b, "answers");
        for (int i = 0; arr && i < g->s.n; i++) {
            int ch = g->ch[i];
            ok_n += ch >= 0 && ch == g->s.q[i].answer;
            cJSON *a = cJSON_CreateObject();
            if (!a) break;
            char cs[2] = {ch >= 0 && ch < 4 ? (char)('A' + ch) : 0, 0};
            cJSON_AddStringToObject(a, "id", g->s.q[i].id);
            cJSON_AddStringToObject(a, "choice", cs);
            cJSON_AddItemToArray(arr, a);
        }
        body = cJSON_PrintUnformatted(b);
        cJSON_Delete(b);
    }
    if (correct) *correct = ok_n;
    return body;
}

/* ket qua phien -> outbox (fd_deck gui khi online; 409 = da nhan) va giai phong slot. Goi khi KHONG giu khoa. */
static void seg_submit(seg_t *g)
{
    int correct = 0;
    char *body = build_result(g, &correct);
    if (body) fd_deck_outbox_add(g->s.session_id, body);
    ESP_LOGI(TAG, "ket qua phien %.12s: %d/%d (lam %d) -> outbox%s", g->s.session_id, correct, g->s.n, g->answered, body ? "" : " LOI");
    free(body);
}

static seg_t *seg_of(int pos)
{
    for (int i = 0; i < SEG_MAX; i++) {
        seg_t *g = &s_seg[i];
        if (g->used && pos >= g->start && pos < g->start + g->s.n) return g;
    }
    return NULL;
}

const fd_study_q_t *fd_study_q(int pos)
{
    if (!init() || pos < 0 || (s_count && pos >= s_count)) return NULL;
    lock();
    seg_t *g = seg_of(pos);
    const fd_study_q_t *q = g ? &g->s.q[pos - g->start] : NULL;
    unlock();
    if (!q) kick();   /* chua nap (vd task cu vua xong): nap tiep */
    return q;   /* slot chi giai phong o luong goi (fd_study_answer/finish) */
}

int fd_study_len(void)
{
    if (!init() || !s_count) return 0;
    lock();
    int n = s_count;
    if (s_dry && !s_busy && s_loaded < n) n = s_loaded;
    unlock();
    return n;
}

bool fd_study_exhausted(void)
{
    if (!init()) return true;
    lock();
    bool e = s_dry && !s_busy;
    unlock();
    return e;
}

void fd_study_answer(int pos, int choice)
{
    if (!init()) return;
    lock();
    seg_t *g = seg_of(pos);
    bool full = false;
    if (g) {
        int k = pos - g->start;
        if (g->ch[k] < 0 && choice >= 0) g->answered++;
        g->ch[k] = (int8_t)(choice >= 0 && choice < 4 ? choice : -1);
        full = g->answered >= g->s.n;
        if (pos + 1 > s_cur) s_cur = pos + 1;
    }
    unlock();
    if (g) {
        if (full) {   /* lam het phien: ket qua day du -> outbox, giai phong slot (khoa: chi luong UI giai phong) */
            seg_submit(g);
            lock();
            g->used = false;
            unlock();
        } else {
            char *body = build_result(g, NULL);   /* tien do .part (mat dien / thoat giua chung van gui duoc) */
            if (body) fd_deck_outbox_progress(g->s.session_id, body);
            free(body);
        }
    }
    kick();
}

static void finish_all(bool complete)
{
    lock();
    s_gen++;   /* bo phien dang nap */
    unlock();
    for (int i = 0; i < SEG_MAX; i++) {
        seg_t *g = &s_seg[i];
        if (!g->used) continue;
        if (g->answered == 0) { if (g->from_cache) fd_deck_cache_unuse(g->s.session_id); }   /* chua lam: tra lai cache */
        else if (complete) seg_submit(g);   /* .part da co: !complete giu tien do (gui sau khi khoi dong lai) */
        lock();
        g->used = false;
        unlock();
    }
    lock();
    s_loaded = s_cur = 0;
    s_st.state = FD_STUDY_IDLE;
    unlock();
}

void fd_study_finish(bool complete)
{
    if (init()) finish_all(complete);
}

bool fd_study_start(const char *direction, int count, const char *source)
{
    if (!init()) return false;
    finish_all(false);   /* task nap cu (neu con) tu dung theo s_gen */
    lock();
    strlcpy(s_dir, !strcmp(direction ? direction : "", "vi-en") ? "vi-en" : "en-vi", sizeof(s_dir));
    s_local = !strcmp(source ? source : "", "offline");
    s_count = count == 0 ? 0 : (count == 20 ? 20 : 10);
    s_loaded = s_cur = 0;
    s_dry = false;
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = FD_STUDY_LOADING;
    s_gen++;
    unlock();
    fd_deck_init();
    kick();
    return true;
}

void fd_study_get(fd_study_status_t *out)
{
    if (!out) return;
    if (!init()) { memset(out, 0, sizeof(*out)); return; }
    lock();
    *out = s_st;
    unlock();
}

void fd_study_cancel(void)
{
    if (!init()) return;
    lock();
    bool loading = s_st.state == FD_STUDY_LOADING;
    unlock();
    if (loading) finish_all(false);
}
