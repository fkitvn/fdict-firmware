#include <stdlib.h>
/*
 * Dieu huong man hinh + phan phoi su kien.
 * - Moi lan chuyen man: tao lv screen moi, build, load, xoa screen cu (deferred qua lv_async_call).
 * - Nut toan cuc: giu B1 = quay lai man cha. Xoay: Cai dat > Huong xoay man hinh.
 */
#include "esp_log.h"
#include "fd_audio.h"
void ui_ota_init(void);   /* ui_ota.c */
#include "fd_display.h"
#include "fd_fonts.h"
#include "fd_ota.h"
#include "nvs.h"
#include "ui_priv.h"
#include "board_pins.h"
#include <string.h>

static const char *TAG = "fd_ui";

static const ui_screen_t *const s_screens[FD_SCR_COUNT] = {
    [FD_SCR_HOME] = &g_scr_home,
    [FD_SCR_SETTINGS] = &g_scr_settings,
    [FD_SCR_WIFI] = &g_scr_wifi,
    [FD_SCR_ABOUT] = &g_scr_about,
    [FD_SCR_ROTATION] = &g_scr_rotation,
    [FD_SCR_BRIGHTNESS] = &g_scr_brightness,
    [FD_SCR_VOLUME] = &g_scr_volume,
    [FD_SCR_TIMEOUT] = &g_scr_timeout,
    [FD_SCR_FLASHCARD] = &g_scr_flashcard,
    [FD_SCR_STUDY] = &g_scr_study,
    [FD_SCR_SYNC] = &g_scr_sync,
    [FD_SCR_STATS] = &g_scr_stats,
    [FD_SCR_PIN] = &g_scr_pin,
    [FD_SCR_PAIR] = &g_scr_pair,
    [FD_SCR_LANG] = &g_scr_lang,
};

static fd_scr_t s_cur = FD_SCR_COUNT;

/* ---------- Che do nha phat trien (NVS fd/devmode) ---------- */
static bool s_devmode;

bool fd_ui_devmode_get(void) { return s_devmode; }

static void devmode_log(bool on) { ESP_LOGI(TAG, "devmode: %s", on ? "on" : "off"); }

/* ---------- Toast ---------- */
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void toast_gone_cb(lv_anim_t *a) { lv_obj_delete(a->var); if (s_toast == a->var) s_toast = NULL; }

static void toast_hide_cb(lv_timer_t *t)
{
    (void)t;
    s_toast_timer = NULL;
    if (!s_toast) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_toast);
    lv_anim_set_exec_cb(&a, toast_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_completed_cb(&a, toast_gone_cb);
    lv_anim_start(&a);
}

void fd_ui_toast(const char *text)
{
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(s_toast, &fd_font_noto_14, 0);
        lv_obj_set_style_text_color(s_toast, lv_color_white(), 0);
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x111827), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_80 + 13, 0);   /* ~85% */
        lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(s_toast, 14, 0);
        lv_obj_set_style_pad_ver(s_toast, 6, 0);
        lv_obj_set_clickable(s_toast, false);
        lv_obj_set_style_opa(s_toast, LV_OPA_TRANSP, 0);
    }
    lv_anim_delete(s_toast, toast_opa_cb);   /* dang mo dan -> hien lai */
    lv_label_set_text(s_toast, text);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_toast);
    lv_anim_set_exec_cb(&a, toast_opa_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa(s_toast, 0), LV_OPA_COVER);
    lv_anim_set_duration(&a, 150);
    lv_anim_start(&a);
    if (s_toast_timer) lv_timer_reset(s_toast_timer);
    else {
        s_toast_timer = lv_timer_create(toast_hide_cb, 1200, NULL);
        lv_timer_set_repeat_count(s_toast_timer, 1);
    }
}

/* (thoi gian cho: xem ben duoi) */
#define HOME_TOAST "Bạn đã ở màn hình Home"

/* Theme: default voi mau chu dao amber; chu tren nut = toi (FD_COLOR_ON_PRIMARY) cho du tuong phan */
static lv_style_t s_btn_text;
static void theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    (void)th;
    if (lv_obj_check_type(obj, &lv_button_class)) lv_obj_add_style(obj, &s_btn_text, 0);
}

static void theme_init(void)
{
    lv_display_t *d = fd_display_get();
    lv_theme_t *base = lv_theme_default_init(d, lv_color_hex(FD_COLOR_PRIMARY), lv_color_hex(FD_COLOR_PRIMARY_DARK),
                                             false, &fd_font_noto_14);
    lv_style_init(&s_btn_text);
    lv_style_set_text_color(&s_btn_text, lv_color_hex(FD_COLOR_ON_PRIMARY));
    lv_theme_t *th = lv_theme_create();
    lv_theme_copy(th, base);
    lv_theme_set_parent(th, base);
    lv_theme_set_apply_cb(th, theme_apply_cb);
    lv_display_set_theme(d, th);
}

static void devmode_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0;   /* mac dinh TAT che do nha phat trien khi NVS chua co khoa */
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "devmode", &v);
        nvs_close(h);
    }
    s_devmode = v != 0;
    devmode_log(s_devmode);
}

void ui_devmode_set(bool on)
{
    s_devmode = on;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "devmode", on ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    devmode_log(on);
    ui_swipe_overlay_sync();   /* overlay vung vuot theo che do nha phat trien, hien/an ngay */
}

const ui_screen_t *ui_nav_current(void)
{
    return s_cur < FD_SCR_COUNT ? s_screens[s_cur] : NULL;
}

/* ---------- Thoi gian cho man hinh (NVS fd/scr_to, giay; 0 = khong tat) ----------
 * Khong hoat dong (cham: lv_display_get_inactive_time; nut: trigger_activity) >= timeout -> tat den nen.
 * Cham/nut bat ky -> bat lai, va lan cham/bam danh thuc do bi nuot (khong kich hoat UI). */
#define SCR_TO_DEFAULT 30
static int s_scr_to = SCR_TO_DEFAULT;
static bool s_swallow;                 /* dang nuot lan cham danh thuc cho toi khi nha tay */
static lv_indev_read_cb_t s_touch_read;
static bool s_tp_was_pressed;          /* trang thai cham lan doc truoc (phat hien luc nha tay) */
static void edge_on_release(lv_indev_t *indev);
static void edge_on_move(lv_indev_t *indev);
int ui_sleep_get_timeout(void) { return s_scr_to; }

void ui_sleep_set_timeout(int sec)
{
    s_scr_to = sec < 0 ? 0 : sec > 3600 ? 3600 : sec;
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, "scr_to", (uint16_t)s_scr_to);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "screen: timeout %d s", s_scr_to);
}

static void sleep_load(void)
{
    nvs_handle_t h;
    uint16_t v = SCR_TO_DEFAULT;
    if (nvs_open("fd", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u16(h, "scr_to", &v);
        nvs_close(h);
    }
    s_scr_to = v;
}

static void screen_wake(void)
{
    if (!fd_display_is_asleep()) return;
    lv_display_trigger_activity(NULL);
    fd_display_set_sleep(false);   /* khoi phuc do sang da luu / dang dung */
    ui_splash_show();              /* man chao logo 1 s moi lan bat man */
    fd_audio_reset_codec();        /* thuc khong reboot: reset codec+PA (wake) */
    ESP_LOGI(TAG, "screen: on");
}

static void sleep_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (fd_display_is_asleep() || s_scr_to == 0) return;
    if (fd_audio_is_playing()) {
        lv_display_trigger_activity(NULL);
        return;
    }
    uint32_t idle = lv_display_get_inactive_time(NULL);
    if (idle >= (uint32_t)s_scr_to * 1000u) {
        fd_display_set_sleep(true);
        ESP_LOGI(TAG, "screen: off (idle %us)", (unsigned)(idle / 1000));
    }
}

/* Boc read_cb cua cam ung: khi dang ngu, lan cham dau chi danh thuc va bao "nha" cho LVGL toi khi nha tay */
static void touch_read_wrap(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_touch_read(indev, data);
    if (data->state == LV_INDEV_STATE_PRESSED) {
        if (fd_display_is_asleep()) {
            s_swallow = true;
            screen_wake();
        }
        if (s_swallow || ui_splash_active()) data->state = LV_INDEV_STATE_RELEASED;   /* man chao: khong cham xuyen */
    } else {
        s_swallow = false;
    }
    /* Vuot mep: quyet dinh tai thoi diem nha tay, doc lap voi viec widget/list co nuot su kien hay khong */
    bool pressed = data->state == LV_INDEV_STATE_PRESSED;
    if (s_tp_was_pressed && pressed) edge_on_move(indev);
    if (s_tp_was_pressed && !pressed) edge_on_release(indev);
    s_tp_was_pressed = pressed;
}
/* Goi trong task LVGL (async) */
static void show_now(fd_scr_t id)
{
    if ((unsigned)id >= FD_SCR_COUNT || !s_screens[id]) {   /* man khong co trong bang -> ve Home */
        ESP_LOGW(TAG, "man %d khong co -> Home", (int)id);
        id = FD_SCR_HOME;
    }
    if (s_cur < FD_SCR_COUNT && s_screens[s_cur]->destroy) {
        s_screens[s_cur]->destroy();
    }
    ui_quick_close(false);   /* chuyen man -> dong bang nhanh */
    s_cur = id;
    lv_obj_t *old = lv_screen_active();
    lv_obj_t *scr = lv_obj_create(NULL);
    s_screens[id]->build(scr);
    lv_screen_load(scr);
    if (old && old != scr) {
        lv_obj_delete(old);
    }
    ESP_LOGI(TAG, "Man hinh: %s", s_screens[id]->name);
}

static void set_rot_async_cb(void *arg)
{
    int idx = (int)(intptr_t)arg;
    fd_display_set_rotation(idx, true);
    ESP_LOGI(TAG, "rotate -> %d do (%dx%d)", fd_display_rotation_deg(idx),
             (int)lv_display_get_horizontal_resolution(NULL), (int)lv_display_get_vertical_resolution(NULL));
    show_now(s_cur);   /* dung lai man hien tai theo do phan giai moi */
}

static void refresh_async_cb(void *arg)
{
    (void)arg;
    show_now(s_cur);
}

void ui_nav_refresh(void) { lv_async_call(refresh_async_cb, NULL); }

void ui_nav_set_rotation(int idx) { lv_async_call(set_rot_async_cb, (void *)(intptr_t)(idx & 3)); }   /* 0..3 = 0/90/180/270 do */

/* Lich su dieu huong (14:33): quay lai ve dung man da mo man hien tai (vd Home -> WiFi -> lui = Home).
 * Home la goc: xoa lich su. Rong -> dung parent tinh cua man. */
#define NAV_HIST_MAX 8
static fd_scr_t s_hist[NAV_HIST_MAX];
static int s_hist_n;

static void hist_push_for(fd_scr_t id)
{
    if (id == FD_SCR_HOME) { s_hist_n = 0; return; }
    if (s_cur >= FD_SCR_COUNT || id == s_cur) return;
    for (int i = 0; i < s_hist_n; i++) {   /* man da co trong lich su -> cat ve do (tranh vong lap) */
        if (s_hist[i] == id) { s_hist_n = i; return; }
    }
    if (s_hist_n > 0 && s_hist[s_hist_n - 1] == s_cur) return;
    if (s_hist_n == NAV_HIST_MAX) {   /* day: bo muc cu nhat */
        memmove(&s_hist[0], &s_hist[1], (NAV_HIST_MAX - 1) * sizeof(s_hist[0]));
        s_hist_n--;
    }
    s_hist[s_hist_n++] = s_cur;
}

static void show_async_cb(void *arg)
{
    fd_scr_t id = (fd_scr_t)(intptr_t)arg;
    if (ui_pin_wake_locked() && id != FD_SCR_PIN) return;   /* khoa PIN sau khi thuc: chan moi dieu huong */
    hist_push_for(id);
    show_now(id);
}

void fd_ui_show(fd_scr_t scr)
{
    if (scr >= FD_SCR_COUNT) return;
    if (fd_display_lock(1000)) {
        lv_async_call(show_async_cb, (void *)(intptr_t)scr);
        fd_display_unlock();
    }
}

static void back_async_cb(void *arg)
{
    (void)arg;
    const ui_screen_t *s = ui_nav_current();
    if (!s) return;
    if (ui_quick_is_open()) {   /* quay lai / giu B1: dong bang nhanh truoc */
        ui_quick_close(true);
        return;
    }
    if (s->on_back && s->on_back()) {
        ESP_LOGD(TAG, "Quay lai: %s tu xu ly", s->name);
        return;
    }
    if (s_cur == FD_SCR_HOME) return;
    fd_scr_t to = s_hist_n > 0 ? s_hist[--s_hist_n] : s->parent;
    if (to == FD_SCR_HOME) s_hist_n = 0;
    show_now(to);
}

/* Deferred: khong xoa screen dang xu ly su kien ngay trong callback */
void ui_nav_back(void) { lv_async_call(back_async_cb, NULL); }
void ui_nav_home(void) { if (s_cur != FD_SCR_HOME) lv_async_call(show_async_cb, (void *)(intptr_t)FD_SCR_HOME); }

/* 23/10: hop thong bao loi may chu (dung nguyen cau "message"), xuong dong theo do rong man (doc/ngang), cham de dong, tu an 6 s.
 * 401: token da bi xoa (fd_ota) -> mo man Ghep. 410: mo man Ghep (tu lay ma ghep moi). 429: fd_ota tu cho roi thu lai. */
static lv_obj_t *s_srv_box;
static lv_timer_t *s_srv_t;
static uint32_t s_srv_seen;
static void srv_close(void)
{
    if (s_srv_t) { lv_timer_delete(s_srv_t); s_srv_t = NULL; }
    if (s_srv_box) { lv_obj_delete(s_srv_box); s_srv_box = NULL; }
}
static void srv_close_cb(lv_event_t *e) { (void)e; srv_close(); }
static void srv_timer_cb(lv_timer_t *t) { (void)t; s_srv_t = NULL; if (s_srv_box) { lv_obj_delete(s_srv_box); s_srv_box = NULL; } }
static void srv_msg_poll(void)
{
    char m[200];
    int code = 0;
    uint32_t g = fd_ota_server_msg(m, sizeof(m), &code);
    if (g == s_srv_seen) return;
    s_srv_seen = g;
    if (!m[0]) return;
    srv_close();
    s_srv_box = lv_obj_create(lv_layer_top());
    lv_obj_set_width(s_srv_box, lv_pct(92));
    lv_obj_set_height(s_srv_box, LV_SIZE_CONTENT);
    lv_obj_align(s_srv_box, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(s_srv_box, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(s_srv_box, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_srv_box, 0, 0);
    lv_obj_set_style_radius(s_srv_box, 12, 0);
    lv_obj_set_style_pad_all(s_srv_box, 10, 0);
    lv_obj_set_scrollable(s_srv_box, false);
    lv_obj_t *l = lv_label_create(s_srv_box);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, m);
    lv_obj_add_event_cb(s_srv_box, srv_close_cb, LV_EVENT_CLICKED, NULL);
    s_srv_t = lv_timer_create(srv_timer_cb, 6000, NULL);
    lv_timer_set_repeat_count(s_srv_t, 1);
    if ((code == 401 || code == 410) && s_cur != FD_SCR_PAIR) fd_ui_show(FD_SCR_PAIR);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    srv_msg_poll();
    const ui_screen_t *s = ui_nav_current();
    if (s && s->on_tick) s->on_tick();
}

/* Log LUON BAT moi lan bam nut (tag fd_key): GPIO tho, chi so nut, huong xoay, mau phim (KEY_GPIO_COLOUR) va o
 * tuong ung (COLOUR_CELL) - de doi chieu phim vat ly <-> GPIO tren monitor. Goi truoc lock nen khong bi mat log. */
static void key_log(const fd_evt_button_t *b)
{
    static const int GPIO[4] = {BOARD_BTN1, BOARD_BTN2, BOARD_BTN3, BOARD_BTN4};
    static const char *const EVT[3] = {"click", "double", "long"};
    static const char *const CELL[4] = {"TL", "TR", "BL", "BR"};
    int idx = b->index;
    int rot = fd_display_get_rotation();
    int k = ui_key_colour(idx), cell = ui_key_cell(idx);
    const ui_screen_t *s = ui_nav_current();
    ESP_LOGD("fd_key", "gpio=%d idx=%d (B%d) evt=%s rot=%d (%d deg) colour=%s -> cell=%s screen=%s%s",
             (idx >= 0 && idx < 4) ? GPIO[idx] : -1, idx, idx + 1,
             ((unsigned)b->action < 3) ? EVT[b->action] : "?", rot, fd_display_rotation_deg(rot),
             ui_key_colour_name(k), (cell >= 0 && cell < 4) ? CELL[cell] : "?",
             (s && s->name) ? s->name : "-", fd_display_is_asleep() ? " (asleep: wake only)" : "");
}

static void btn_async(void *p)
{
    fd_evt_button_t *b = p;
    const ui_screen_t *s = ui_nav_current();
    if (!ui_splash_active() && !fd_display_is_asleep()) {
        if (b->index == 0 && b->action == FD_BTN_LONG_PRESS && ui_quick_is_open()) ui_quick_close(true);
        else if (!(s && s->on_button && s->on_button(b)) && b->index == 0 && b->action == FD_BTN_LONG_PRESS && s) {
            if (s_cur == FD_SCR_HOME) fd_ui_toast(HOME_TOAST);
            else ui_nav_back();
        }
    }
    free(b);
}

static void on_fd_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == FD_EVT_BUTTON && data) key_log(data);
    if (!fd_display_lock(200)) {
        return;
    }
    const ui_screen_t *s = ui_nav_current();
    if (id == FD_EVT_BUTTON) {
        const fd_evt_button_t *b = data;
        if (fd_display_is_asleep()) {   /* nut danh thuc: nuot su kien */
            screen_wake();
            fd_display_unlock();
            return;
        }
        if (ui_splash_active()) {   /* man chao: nuot nut */
            fd_display_unlock();
            return;
        }
        lv_display_trigger_activity(NULL);
        /* 23/10: xu ly nut trong task LVGL (lv_async_call), KHONG trong sys_evt (stack 2304 B): tra loi cau hoi ghi
         * tien do outbox (cJSON + littlefs) + am thanh + ve lai man -> tran stack sys_evt -> reset. */
        fd_evt_button_t *cp = malloc(sizeof(*cp));
        if (cp) { *cp = *b; if (lv_async_call(btn_async, cp) != LV_RESULT_OK) free(cp); }
    } else {
        if (s && s->on_event) s->on_event(id, data);
    }
    fd_display_unlock();
}

/* ---------- Vuot tu mep (kieu smartphone) ----------
 * Bat dau trong vung mep trai/phai (FD_SWIPE_EDGE_PX) va vuot vao >= SWIPE_PX (|dx| > |dy|) -> quay lai.
 * Bat dau o mep duoi va vuot len >= SWIPE_PX -> ve Home. O Home: chi hien toast "Ban da o man hinh Home".
 * Xu ly o muc indev (toa do logic) nen khong bi list cuon "nuot" mat. */
#include "ui_swipe.h"
#define SWIPE_PX FD_SWIPE_MIN_PX

typedef enum { EDGE_NONE, EDGE_LEFT, EDGE_RIGHT, EDGE_BOTTOM, EDGE_TOP, EDGE_QUICK } edge_t;
static edge_t s_edge;
static lv_point_t s_edge_start;
static bool s_edge_hit;             /* lan nha nay la cu chi mep -> chan click */
enum { Z_LEFT = 1, Z_RIGHT = 2, Z_TOP = 4, Z_BOTTOM = 8 };
static uint8_t s_zones;            /* cac vung mep chua diem bat dau */
static bool s_gesture_claimed;      /* vuot da gianh quyen: nut duoi tay da bi huy */
static bool s_back_ok;              /* diem bat dau khong nam tren widget keo ngang -> cho phep quay lai */

/* Doi tuong (hoac cha) can keo ngang -> khong bien vuot ngang thanh "quay lai" */
static bool needs_hdrag(lv_obj_t *o)
{
    for (; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_check_type(o, &lv_slider_class) || lv_obj_check_type(o, &lv_bar_class) ||
            lv_obj_check_type(o, &lv_arc_class) ||
            lv_obj_check_type(o, &lv_switch_class) || lv_obj_check_type(o, &lv_textarea_class) ||
            lv_obj_check_type(o, &lv_keyboard_class) || lv_obj_check_type(o, &lv_buttonmatrix_class))   /* roller/dropdown/spinbox: tat trong sdkconfig (khong dung) */
            return true;
        if ((lv_obj_get_scroll_dir(o) & LV_DIR_HOR) && lv_obj_is_scrollable(o) &&
            (lv_obj_get_scroll_left(o) > 0 || lv_obj_get_scroll_right(o) > 0))
            return true;   /* noi dung rong hon -> dang cuon ngang that su */
    }
    return false;
}

static void anim_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void anim_del_cb(lv_anim_t *a) { lv_obj_delete(a->var); }

static void edge_hint(edge_t e)
{
    lv_obj_t *o = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(o, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_50, 0);
    lv_obj_set_style_text_color(o, lv_color_white(), 0);
    lv_obj_set_style_pad_all(o, 8, 0);
    lv_obj_set_style_radius(o, 16, 0);
    lv_label_set_text(o, e == EDGE_BOTTOM ? LV_SYMBOL_HOME : LV_SYMBOL_LEFT);
    lv_obj_align(o, e == EDGE_LEFT ? LV_ALIGN_LEFT_MID : e == EDGE_RIGHT ? LV_ALIGN_RIGHT_MID : LV_ALIGN_BOTTOM_MID,
                 e == EDGE_LEFT ? 6 : e == EDGE_RIGHT ? -6 : 0, e == EDGE_BOTTOM ? -6 : 0);
    lv_obj_set_clickable(o, false);
    /* hien nhanh roi mo dan (~350ms) va tu xoa */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_reverse_duration(&a, 230);
    lv_anim_set_completed_cb(&a, anim_del_cb);
    lv_anim_start(&a);
}

static void edge_indev_cb(lv_event_t *ev)
{
    lv_indev_t *indev = lv_event_get_current_target(ev);
    lv_event_code_t code = lv_event_get_code(ev);
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        int32_t w = lv_display_get_horizontal_resolution(NULL);
        int32_t h = lv_display_get_vertical_resolution(NULL);
        s_edge = EDGE_NONE;
        s_zones = 0;
        s_back_ok = false;
        s_gesture_claimed = false;
        s_edge_start = p;
        if (s_cur >= FD_SCR_COUNT) return;
        if (ui_quick_is_open()) {   /* bang nhanh mo: chi cho vuot len tren bang de dong, mep khac khong dieu huong */
            if (ui_quick_hit(&p)) s_edge = EDGE_QUICK;
            return;
        }
        /* diem lech ra ngoai man <= FD_SWIPE_TOL_PX van tinh: kep ve 0..max truoc khi xet vung */
        if (p.x < 0 && p.x >= -FD_SWIPE_TOL_PX) p.x = 0;
        if (p.y < 0 && p.y >= -FD_SWIPE_TOL_PX) p.y = 0;
        if (p.x >= w && p.x < w + FD_SWIPE_TOL_PX) p.x = w - 1;
        if (p.y >= h && p.y < h + FD_SWIPE_TOL_PX) p.y = h - 1;
        /* ghi tat ca vung mep chua diem bat dau; o goc huong vuot luc nha tay se chon cu chi */
        s_zones = 0;
        {
            lv_obj_t *o = lv_indev_get_active_obj();
            if (!o) o = lv_indev_search_obj(lv_layer_top(), &p);
            if (!o) o = lv_indev_search_obj(lv_screen_active(), &p);
            s_back_ok = !needs_hdrag(o);   /* ngoai le ap dung cho moi vuot quay lai */
        }
        if (p.y < FD_SWIPE_TOP_PX) s_zones |= Z_TOP;
        if (p.x < FD_SWIPE_EDGE_PX) s_zones |= Z_LEFT;
        if (p.x >= w - FD_SWIPE_EDGE_PX) s_zones |= Z_RIGHT;
        if (FD_SWIPE_BOTTOM_PX > 0 && p.y >= h - FD_SWIPE_BOTTOM_PX) s_zones |= Z_BOTTOM;   /* 0 = tat vuot len -> Home */
        /* s_edge_start giu diem THO (dong 402): kep TOL chi de xet vung, khong doi quang duong -> nguong dung >= FD_SWIPE_MIN_PX */
        s_edge_hit = false;
    } else if (code == LV_EVENT_RELEASED) {
        /* Quyet dinh da lam o touch_read_wrap (luon chay). O day chi chan RELEASED/CLICKED toi widget ben duoi. */
        if (s_edge_hit) {
            s_edge_hit = false;
            lv_indev_stop_processing(indev);
        }
    }
}

static void edge_act_async(void *arg)
{
    edge_t e = (edge_t)(intptr_t)arg;
    if (e == EDGE_TOP) {
        ESP_LOGD(TAG, "Vuot mep tren -> bang cai dat nhanh");
        ui_quick_open();
        return;
    }
    if (e == EDGE_QUICK) {
        ui_quick_close(true);
        return;
    }
    ESP_LOGD(TAG, "Vuot mep %s", e == EDGE_BOTTOM ? "duoi -> Home" : "-> quay lai");
    if (s_cur == FD_SCR_HOME) {   /* da o Home: chi bao toast, khong hien goi y */
        fd_ui_toast(HOME_TOAST);
        return;
    }
    edge_hint(e);
    if (e == EDGE_BOTTOM) ui_nav_home();
    else ui_nav_back();
}

/* Goi tu touch_read_wrap khi ngon tay vua nha (chuyen PRESSED -> RELEASED).
 * KHONG dua vao su kien RELEASED cua indev: LVGL 9.6 chi gui RELEASED (ca toi indev cb) khi con act_obj;
 * khi vuot qua list cuon / nut (Flashcard, Cai dat) act_obj bi xoa (PRESS_LOST / scroll) -> cu chi bi mat. */
static void edge_on_release(lv_indev_t *indev)
{
    s_edge_hit = false;
    lv_point_t p;
    lv_indev_get_point(indev, &p);   /* diem cuoi cung LVGL da xu ly (toa do logic) */
    int32_t dx = p.x - s_edge_start.x, dy = p.y - s_edge_start.y;
    int32_t adx = LV_ABS(dx), ady = LV_ABS(dy);
    edge_t e = s_edge;
    s_edge = EDGE_NONE;
    bool hit = false;
    uint8_t z = s_zones;
    s_zones = 0;
    bool back_ok = s_back_ok;
    s_back_ok = false;
    /* Truc chinh troi hon la du (~45 do) - truoc day doi |dx| > 2|dy| (~27 do) nen vuot ngon cai hoi cheo bi loai */
    if (e == EDGE_QUICK) {
        hit = -dy >= SWIPE_PX && ady > adx;
    } else if (adx > ady) {
        if ((z & Z_LEFT) && back_ok && dx >= SWIPE_PX) { e = EDGE_LEFT; hit = true; }
        else if ((z & Z_RIGHT) && back_ok && -dx >= SWIPE_PX) { e = EDGE_RIGHT; hit = true; }
        else if (FD_SWIPE_BACK_ANYWHERE && back_ok && adx >= SWIPE_PX) { e = dx > 0 ? EDGE_LEFT : EDGE_RIGHT; hit = true; }   /* bat ky dau */
    } else if (ady > adx) {
        if ((z & Z_TOP) && back_ok && dy >= SWIPE_PX) { e = EDGE_TOP; hit = true; }
        else if ((z & Z_BOTTOM) && back_ok && -dy >= SWIPE_PX) { e = EDGE_BOTTOM; hit = true; }
    }
    if (!hit) return;
    s_edge_hit = true;   /* edge_indev_cb(RELEASED) se chan click toi widget */
    lv_async_call(edge_act_async, (void *)(intptr_t)e);
}
/* Goi tu touch_read_wrap moi lan doc khi van dang cham. Vuot uu tien hon nut: bat dau trong vung mep (khong phai
 * widget ngoai le) va di > FD_SWIPE_TAP_SLOP_PX dung huong cu chi cua vung do -> lv_indev_wait_release():
 * LVGL gui PRESS_LOST (bo trang thai nhan), khong CLICKED / SHORT_CLICKED / LONG_PRESSED, dung cuon.
 * Huong nguoc (vd keo list len khi bat dau o mep tren) khong bi huy -> list van cuon. */
static void edge_on_move(lv_indev_t *indev)
{
    if (s_gesture_claimed || !s_zones || !s_back_ok) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t dx = p.x - s_edge_start.x, dy = p.y - s_edge_start.y;
    int32_t adx = LV_ABS(dx), ady = LV_ABS(dy);
    if (LV_MAX(adx, ady) <= FD_SWIPE_TAP_SLOP_PX) return;
    bool g = false;
    if (adx > ady) g = ((s_zones & Z_LEFT) && dx > 0) || ((s_zones & Z_RIGHT) && dx < 0);
    else if (ady > adx) g = ((s_zones & Z_TOP) && dy > 0) || ((s_zones & Z_BOTTOM) && dy < 0);
    if (!g) return;
    s_gesture_claimed = true;
    lv_indev_wait_release(indev);
}

/* Overlay vung vuot mep (den 20%, rong theo FD_SWIPE_TOP/BOTTOM/EDGE_PX) tren lv_layer_top: hien khi che do nha phat trien BAT.
 * Tao 1 lan luc khoi dong (nam DUOI bang nhanh/toast/hop thoai), hoan toan khong tuong tac. */
static lv_obj_t *s_zone_ov[4];

static void swipe_overlay_create(void)
{
    /* can le + kich thuoc % de tu dung khi xoay man (layer_top doi kich thuoc) */
    static const lv_align_t al[4] = {LV_ALIGN_TOP_MID, LV_ALIGN_BOTTOM_MID, LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = s_zone_ov[i] = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(o);
        if (i < 2) lv_obj_set_size(o, lv_pct(100), i == 0 ? FD_SWIPE_TOP_PX : FD_SWIPE_BOTTOM_PX);
        else lv_obj_set_size(o, FD_SWIPE_EDGE_PX, lv_pct(100));
        lv_obj_align(o, al[i], 0, 0);
        lv_obj_set_style_bg_color(o, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_20, 0);
        lv_obj_set_clickable(o, false);
        lv_obj_set_scrollable(o, false);
        lv_obj_set_event_bubble(o, false);
        lv_obj_set_click_focusable(o, false);
        lv_obj_set_gesture_bubble(o, false);
    }
    ui_swipe_overlay_sync();
}

/* Goi trong task LVGL (hoac khi giu khoa display) */
void ui_swipe_overlay_sync(void)
{
    for (int i = 0; i < 4; i++)
        if (s_zone_ov[i]) lv_obj_set_hidden(s_zone_ov[i], !s_devmode);
}

esp_err_t fd_ui_start(fd_scr_t first)
{
    if (!fd_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    devmode_load();
    sleep_load();
    theme_init();
    ui_ota_init();   /* fd_ota: hook ranh + thong bao khoi dong lai */
    ui_pin_wake_init();
    if (ui_pin_wake_locked()) first = FD_SCR_PIN;   /* thuc sau khi nguoi dung cho ngu + co PIN */
    show_now(first);
    swipe_overlay_create();
    ui_splash_show();   /* man chao luc khoi dong, sau do la man first (Home) */
    lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_create(sleep_timer_cb, 500, NULL);
    lv_indev_t *ti = fd_display_touch_indev();
    if (ti) {
        s_touch_read = lv_indev_get_read_cb(ti);
        if (s_touch_read) lv_indev_set_read_cb(ti, touch_read_wrap);
        lv_indev_add_event_cb(ti, edge_indev_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(ti, edge_indev_cb, LV_EVENT_RELEASED, NULL);
    }
    fd_display_unlock();
    fd_audio_reset_codec();   /* boot (ke ca sau nap fw / thuc tu deep sleep): reset codec+PA */
    return esp_event_handler_register(FD_EVENT, ESP_EVENT_ANY_ID, on_fd_event, NULL);
}
