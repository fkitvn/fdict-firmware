/* Nghe: cau AI cua tu trong bo da chon. Phat ca cau, nhap chu cai dau moi tu (nhu ma PIN).
 * Bo ky tu dac biet, khong phan biet hoa thuong. Moi lan Gợi ý hien them chu cai dau cua tu tiep theo.
 * Dung thi hien ca cau. Bo qua chuyen cau tiep. Ket qua gui kem loi cau (fd_sync_event_sen). */
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fd_fonts.h"
#include "fd_ota.h"
#include "fd_sentence.h"
#include "fd_sync.h"
#include "fd_t9kb.h"
#include "nvs.h"
#include "ui_priv.h"

#define C_OK  0x16A34A
#define C_BAD 0xDC2626
#define CNT_KEY "hear_cnt"

typedef enum { V_SETUP, V_LOAD, V_MSG, V_PLAY, V_RESULT } view_t;

typedef struct {
    char wid[FD_SYNC_WID_LEN];
    char sentence[FD_SEN_TXT_LEN];
    char hint[FD_SEN_TXT_LEN];
    char key[FD_SEN_TOK + 1];
    uint8_t n;
} hear_t;

static view_t s_view;
static lv_obj_t *s_body, *s_kb, *s_ta, *s_slots, *s_sent, *s_vi, *s_skip, *s_prompt, *s_dlg;
static lv_obj_t *s_box[FD_SEN_TOK], *s_ch[FD_SEN_TOK];
static hear_t *s_items;
static int s_n, s_pos, s_correct, s_cnt = 10;
static int s_typed, s_hints;
static char s_buf[FD_SEN_TOK + 1];
static bool s_solved, s_shake, s_gate, s_guard;
static SemaphoreHandle_t s_mx;
static volatile int s_gen;
static volatile bool s_loading;
static int s_load_err;

static void show_setup(void);
static void show_question(void);
static void show_result(void);
static void kb_enable(bool on);

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
}

static int key_of(const fd_sen_ex_t *e, char *key, int cap)
{
    int n = 0;
    for (int i = 0; i < e->n && n + 1 < cap; i++) {
        char c = 0;
        for (const unsigned char *p = (const unsigned char *)e->ans[i]; *p; p++) {
            unsigned char ch = *p;
            if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
            if (ch >= 'a' && ch <= 'z') { c = (char)ch; break; }
        }
        if (c) key[n++] = c;
    }
    key[n] = 0;
    return n;
}

static void prefs_load(void)
{
    nvs_handle_t h;
    uint8_t c = 10;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, CNT_KEY, &c);
        nvs_close(h);
    }
    s_cnt = (c == 0 || c == 20) ? c : 10;
}

static void pref_save(uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, CNT_KEY, v);
    nvs_commit(h);
    nvs_close(h);
}

static void say_current(void)
{
    if (s_pos < 0 || s_pos >= s_n || !s_items[s_pos].sentence[0]) return;
    const fd_pron_cfg_t cfg = {FD_PRON_PICO_ONLY, 1, false};
    ui_pron_say_cfg(s_items[s_pos].sentence, &cfg);
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, t);
    return l;
}

static void body_clear(void)
{
    if (s_dlg) { lv_obj_delete(s_dlg); s_dlg = NULL; }
    lv_obj_clean(s_body);
    s_kb = s_ta = s_slots = s_sent = s_vi = s_skip = s_prompt = NULL;
    memset(s_box, 0, sizeof(s_box));
    memset(s_ch, 0, sizeof(s_ch));
}

static void paint_slots(void)
{
    const hear_t *h = (s_pos >= 0 && s_pos < s_n) ? &s_items[s_pos] : NULL;
    for (int i = 0; i < FD_SEN_TOK; i++) {
        if (!s_box[i] || !h || i >= h->n) continue;
        char txt[2] = {0, 0};
        uint32_t col = FD_COLOR_TEXT;
        uint32_t border = 0x9CA3AF;
        bool fill = false;
        if (s_solved) {
            txt[0] = (char)(h->key[i] - 'a' + 'A');
            col = C_OK;
            border = C_OK;
            fill = true;
        } else if (i < s_typed) {
            txt[0] = (char)(s_buf[i] - 'a' + 'A');
            border = FD_COLOR_PRIMARY;
            fill = true;
        } else if (i < s_hints) {
            txt[0] = (char)(h->key[i] - 'a' + 'A');
            col = FD_COLOR_TEXT_MUTED;
            border = FD_COLOR_TEXT_MUTED;
        }
        lv_label_set_text(s_ch[i], txt);
        lv_obj_set_style_text_color(s_ch[i], lv_color_hex(col), 0);
        lv_obj_set_style_border_color(s_box[i], lv_color_hex(border), 0);
        lv_obj_set_style_bg_color(s_box[i], lv_color_hex(fill ? FD_COLOR_PRIMARY_TINT : 0xFFFFFF), 0);
    }
    if (s_sent && h) {
        lv_label_set_text(s_sent, s_solved ? h->sentence : "");
        lv_obj_set_hidden(s_sent, !s_solved);
    }
    if (s_vi) {
        bool show = s_solved && h && h->hint[0];
        if (show) lv_label_set_text(s_vi, h->hint);
        lv_obj_set_hidden(s_vi, !show);
    }
    if (s_prompt) lv_obj_set_hidden(s_prompt, s_solved);
    if (s_slots) lv_obj_set_hidden(s_slots, s_solved);
    kb_enable(!s_solved);
    if (s_skip) lv_label_set_text(lv_obj_get_child(s_skip, 0), s_solved ? _("NEXT") : _("HEAR_SKIP"));
}

static void shake_done(lv_anim_t *a)
{
    (void)a;
    s_shake = false;
    s_typed = 0;
    memset(s_buf, 0, sizeof(s_buf));
    s_guard = true;
    if (s_ta) lv_textarea_set_text(s_ta, "");
    s_guard = false;
    paint_slots();
}

static void shake_cb(void *o, int32_t v)
{
    static const int8_t X[] = {0, -8, 8, -6, 6, -3, 3, 0};
    lv_obj_set_style_translate_x((lv_obj_t *)o, X[v < 0 ? 0 : v > 7 ? 7 : v], 0);
}

static void slots_error(void)
{
    if (!s_slots) return;
    s_shake = true;
    s_guard = true;
    if (s_ta) lv_textarea_set_text(s_ta, "");
    s_guard = false;
    kb_enable(false);
    for (int i = 0; i < s_typed && i < FD_SEN_TOK; i++)
        if (s_box[i]) lv_obj_set_style_border_color(s_box[i], lv_color_hex(C_BAD), 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_slots);
    lv_anim_set_exec_cb(&a, shake_cb);
    lv_anim_set_values(&a, 0, 7);
    lv_anim_set_duration(&a, 280);
    lv_anim_set_completed_cb(&a, shake_done);
    lv_anim_start(&a);
}

static void finish_ok(void)
{
    if (s_solved || s_pos < 0 || s_pos >= s_n) return;
    s_solved = true;
    s_correct++;
    fd_sync_event_sen(s_items[s_pos].wid, s_items[s_pos].sentence, true, "hear");
    paint_slots();
}

static void check_buf(void)
{
    if (s_solved || s_pos < 0 || s_pos >= s_n) return;
    const hear_t *h = &s_items[s_pos];
    if (s_typed != h->n) return;
    if (strcmp(s_buf, h->key) == 0) finish_ok();
    else slots_error();
}

static void kb_enable(bool on)
{
    if (!s_kb) return;
    uint32_t n = lv_obj_get_child_count(s_kb);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(s_kb, i);
        if (on) lv_obj_remove_state(c, LV_STATE_DISABLED);
        else lv_obj_add_state(c, LV_STATE_DISABLED);
    }
}

/* Ban phim Tu dien ghi vao textarea an. Chi giu chu a-z, du do thi tu doi chieu. */
static void ta_cb(lv_event_t *e)
{
    (void)e;
    if (s_guard || s_shake || s_solved || s_view != V_PLAY || !s_ta) return;
    if (s_pos < 0 || s_pos >= s_n) return;
    int maxn = s_items[s_pos].n;
    const char *t = lv_textarea_get_text(s_ta);
    char clean[FD_SEN_TOK + 1];
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)t; *p && n < maxn && n < FD_SEN_TOK; p++) {
        unsigned char ch = *p;
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch - 'A' + 'a');
        if (ch >= 'a' && ch <= 'z') clean[n++] = (char)ch;
    }
    clean[n] = 0;
    if (strcmp(t, clean) != 0) {
        s_guard = true;
        lv_textarea_set_text(s_ta, clean);
        s_guard = false;
    }
    memcpy(s_buf, clean, (size_t)n + 1);
    s_typed = n;
    paint_slots();
    if (n == maxn) check_buf();
}

static void hint_cb(lv_event_t *e)
{
    (void)e;
    if (s_solved || s_shake || s_view != V_PLAY || s_pos < 0 || s_pos >= s_n) return;
    if (s_hints < s_items[s_pos].n) s_hints++;
    paint_slots();
}

static void next_question(void)
{
    if (!s_solved && s_pos >= 0 && s_pos < s_n)
        fd_sync_event_sen(s_items[s_pos].wid, s_items[s_pos].sentence, false, "hear");
    if (++s_pos >= s_n) show_result();
    else show_question();
}

static void skip_cb(lv_event_t *e) { (void)e; if (s_view == V_PLAY) next_question(); }
static void speak_cb(lv_event_t *e) { (void)e; say_current(); }

static lv_obj_t *text_act(lv_obj_t *row, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 36);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY_TINT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(FD_COLOR_TEXT), 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void show_question(void)
{
    s_view = V_PLAY;
    s_solved = false;
    s_shake = false;
    s_guard = false;
    s_typed = s_hints = 0;
    memset(s_buf, 0, sizeof(s_buf));
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_body, 4, 0);

    lv_obj_t *stage = lv_obj_create(s_body);
    lv_obj_remove_style_all(stage);
    lv_obj_set_width(stage, lv_pct(100));
    lv_obj_set_height(stage, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(stage, 78, 0);
    lv_obj_set_flex_flow(stage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stage, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(stage, 2, 0);

    lv_obj_t *head = lv_obj_create(stage);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 8, 0);

    char prog[16];
    snprintf(prog, sizeof(prog), "%d / %d", s_pos + 1, s_n);
    lv_obj_t *pl = lv_label_create(head);
    lv_obj_set_style_text_font(pl, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(pl, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(pl, prog);

    s_prompt = lv_label_create(head);
    lv_obj_set_style_text_font(s_prompt, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(s_prompt, lv_color_hex(FD_COLOR_TEXT_MUTED), 0);
    lv_obj_set_flex_grow(s_prompt, 1);
    lv_label_set_long_mode(s_prompt, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_prompt, _("HEAR_PROMPT"));

    s_slots = lv_obj_create(stage);
    lv_obj_remove_style_all(s_slots);
    lv_obj_set_width(s_slots, lv_pct(100));
    lv_obj_set_height(s_slots, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_slots, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_slots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_slots, 4, 0);
    lv_obj_set_style_pad_row(s_slots, 4, 0);
    int nlet = s_items[s_pos].n;
    for (int i = 0; i < nlet; i++) {
        lv_obj_t *b = lv_obj_create(s_slots);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 26, 28);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        s_box[i] = b;
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
        lv_obj_center(l);
        s_ch[i] = l;
    }

    s_sent = label(stage, "", &fd_font_noto_14, FD_COLOR_TEXT);
    s_vi = label(stage, "", &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_set_hidden(s_sent, true);
    lv_obj_set_hidden(s_vi, true);

    lv_obj_t *acts = lv_obj_create(s_body);
    lv_obj_remove_style_all(acts);
    lv_obj_set_width(acts, lv_pct(100));
    lv_obj_set_height(acts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(acts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(acts, 6, 0);

    lv_obj_t *spk = lv_button_create(acts);
    lv_obj_set_size(spk, 36, 36);
    lv_obj_set_style_radius(spk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(spk, 0, 0);
    lv_obj_set_style_shadow_width(spk, 0, 0);
    lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY_DARK), LV_STATE_PRESSED);
    lv_obj_add_event_cb(spk, speak_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *si = lv_label_create(spk);
    lv_obj_set_style_text_font(si, &fd_font_mont_24, 0);
    lv_obj_set_style_text_color(si, lv_color_white(), 0);
    lv_label_set_text(si, LV_SYMBOL_VOLUME_MAX);
    lv_obj_center(si);

    text_act(acts, _("HEAR_HINT"), hint_cb);
    s_skip = text_act(acts, _("HEAR_SKIP"), skip_cb);

    s_ta = lv_textarea_create(s_body);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 24);
    lv_obj_set_hidden(s_ta, true);
    lv_obj_set_ignore_layout(s_ta, true);
    lv_obj_add_event_cb(s_ta, ta_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_kb = fd_t9kb_create(s_body, s_ta, NULL, NULL);
    fd_t9kb_set_qwerty_plain(s_kb);
    lv_obj_set_width(s_kb, lv_pct(100));
    lv_obj_set_flex_grow(s_kb, 1);
    lv_obj_set_style_min_height(s_kb, 112, 0);

    paint_slots();
    say_current();
}

static void load_task(void *arg)
{
    int gen = (int)(intptr_t)arg;
    int cap = s_cnt == 0 ? FD_SEN_MAX : s_cnt;
    fd_sen_ex_t *tmp = ps_alloc((size_t)cap * sizeof(fd_sen_ex_t));
    char (*wid)[FD_SYNC_WID_LEN] = ps_alloc((size_t)cap * FD_SYNC_WID_LEN);
    int err = 2, got = 0;
    if (tmp && wid && s_items) {
        int n = fd_sync_build_sentences(tmp, wid, cap);
        err = n > 0 ? 0 : 1;
        for (int i = 0; i < n && got < FD_SEN_MAX; i++) {
            char key[FD_SEN_TOK + 1];
            int k = key_of(&tmp[i], key, sizeof(key));
            if (k < 2 || !wid[i][0]) continue;
            hear_t *h = &s_items[got];
            memset(h, 0, sizeof(*h));
            strlcpy(h->wid, wid[i], sizeof(h->wid));
            strlcpy(h->sentence, tmp[i].sentence, sizeof(h->sentence));
            strlcpy(h->hint, tmp[i].hint, sizeof(h->hint));
            strlcpy(h->key, key, sizeof(h->key));
            h->n = (uint8_t)k;
            got++;
        }
        if (!got) err = 1;
    }
    free(tmp);
    free(wid);
    if (s_mx) xSemaphoreTake(s_mx, portMAX_DELAY);
    if (gen == s_gen) {
        s_n = got;
        s_load_err = err;
        s_loading = false;
    }
    if (s_mx) xSemaphoreGive(s_mx);
    vTaskDelete(NULL);
}

static void show_load(void)
{
    s_view = V_LOAD;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sp = lv_spinner_create(s_body);
    lv_obj_set_size(sp, 40, 40);
    label(s_body, _("SEN_LOADING"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
}

static void setup_cb(lv_event_t *e) { (void)e; show_setup(); }

static void show_msg(void)
{
    s_view = V_MSG;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(s_body, _("NO_OFFLINE_DATA_YET"), &fd_font_noto_18, FD_COLOR_TEXT);
    label(s_body, _("SYNC_AUTO_HINT"), &fd_font_noto_14, FD_COLOR_TEXT_MUTED);
    lv_obj_t *b = lv_button_create(s_body);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_label_set_text(l, _("BACK"));
    lv_obj_center(l);
    lv_obj_add_event_cb(b, setup_cb, LV_EVENT_CLICKED, NULL);
}

static void show_result(void)
{
    s_view = V_RESULT;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), _("SEN_RESULT"), s_correct, s_n);
    label(s_body, _("LISTEN"), &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    label(s_body, tmp, &fd_font_mont_32, s_correct * 2 >= s_n ? C_OK : C_BAD);
    lv_obj_t *b = lv_button_create(s_body);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_label_set_text(l, _("BACK"));
    lv_obj_center(l);
    lv_obj_add_event_cb(b, setup_cb, LV_EVENT_CLICKED, NULL);
}

static void start(void)
{
    if (fd_sync_pick_count(FD_SYNC_PICK_F_SEN) <= 0) { fd_ui_toast(_("NO_DECK_WORDS")); return; }
    if (!s_mx) s_mx = xSemaphoreCreateMutex();
    if (!s_items) s_items = ps_alloc(FD_SEN_MAX * sizeof(hear_t));
    if (!s_mx || !s_items) { show_msg(); return; }
    if (xSemaphoreTake(s_mx, 0) != pdTRUE) return;
    if (s_loading) { xSemaphoreGive(s_mx); return; }
    s_pos = s_correct = 0;
    s_gen++;
    int gen = s_gen;
    s_loading = true;
    s_load_err = 0;
    xSemaphoreGive(s_mx);
    show_load();
    if (xTaskCreate(load_task, "fd_hear", 8192, (void *)(intptr_t)gen, 4, NULL) != pdPASS) {
        s_loading = false;
        show_msg();
    }
}

static void poll_load(void)
{
    if (s_view != V_LOAD || !s_body || !s_mx) return;
    if (xSemaphoreTake(s_mx, 0) != pdTRUE) return;
    bool loading = s_loading;
    int err = s_load_err;
    int n = s_n;
    xSemaphoreGive(s_mx);
    if (loading) return;
    s_n = n;
    if (err || s_n <= 0) show_msg();
    else show_question();
}

static void setup_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_setup(); }
static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int v = (int)(ud & 0xF);
    static const int NOPT[3] = {10, 20, 0};
    s_cnt = NOPT[v < 3 ? v : 0];
    pref_save((uint8_t)s_cnt);
    lv_async_call(setup_async, NULL);
}
static void begin_cb(lv_event_t *e) { (void)e; start(); }

static void show_setup(void)
{
    s_view = V_SETUP;
    body_clear();
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sc = ui_setup_begin(s_body, _("LISTEN"));
    ui_setup_deck(sc, setup_async);
    static const char *const CNT[] = {"10", "20", "ENDLESS"};
    int nsel = s_cnt == 0 ? 2 : (s_cnt == 20 ? 1 : 0);
    ui_setup_opt_row(sc, _("QUESTIONS"), CNT, 3, nsel, opt_cb, 0, 0);
    ui_setup_start(s_body, begin_cb);
}

static void dlg_close(void)
{
    if (!s_dlg) return;
    lv_obj_delete(s_dlg);
    s_dlg = NULL;
}

static void dlg_yes(lv_event_t *e) { (void)e; dlg_close(); show_setup(); }
static void dlg_no(lv_event_t *e) { (void)e; dlg_close(); }

static void dlg_open(void)
{
    if (s_dlg || !s_body) return;
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dlg);
    lv_obj_set_size(s_dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_40, 0);
    lv_obj_set_clickable(s_dlg, true);
    lv_obj_t *box = lv_obj_create(s_dlg);
    lv_obj_set_width(box, lv_pct(88));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_center(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 8, 0);
    label(box, _("QUIT_THIS_QUIZ"), &fd_font_noto_18, FD_COLOR_TEXT);
    lv_obj_t *r = lv_obj_create(box);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_t *a = lv_button_create(r);
    lv_obj_t *al = lv_label_create(a);
    lv_obj_set_style_text_font(al, &fd_font_noto_14, 0);
    lv_label_set_text(al, _("CANCEL"));
    lv_obj_center(al);
    lv_obj_add_event_cb(a, dlg_no, LV_EVENT_CLICKED, NULL);
    lv_obj_t *y = lv_button_create(r);
    lv_obj_t *yl = lv_label_create(y);
    lv_obj_set_style_text_font(yl, &fd_font_noto_14, 0);
    lv_label_set_text(yl, _("QUIT"));
    lv_obj_center(yl);
    lv_obj_add_event_cb(y, dlg_yes, LV_EVENT_CLICKED, NULL);
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    s_gate = !fd_ota_is_paired();
    if (s_gate) { ui_pair_gate(s_body); return; }
    prefs_load();
    show_setup();
}

static void destroy(void)
{
    if (s_mx && xSemaphoreTake(s_mx, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_gen++;
        s_loading = false;
        xSemaphoreGive(s_mx);
    } else s_gen++;
    dlg_close();
    s_body = NULL;
}

static void on_tick(void) { if (!s_gate) poll_load(); }

static bool on_back(void)
{
    if (s_gate) return false;
    if (s_dlg) { dlg_close(); return true; }
    if (s_view == V_PLAY) { dlg_open(); return true; }
    if (s_view != V_SETUP) { show_setup(); return true; }
    return false;
}

const ui_screen_t g_scr_listen = {
    .name = "listen", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_tick = on_tick, .on_back = on_back,
};
