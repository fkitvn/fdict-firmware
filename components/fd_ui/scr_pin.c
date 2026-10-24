/* Ma PIN 4 so (FD_SCR_PIN) - giao dien 16:40.
 * Doc 240x320: tieu de 'Mã PIN' 32 px, dong nhac, 4 cham lon (+ thanh dem nguoc khi khoa), ban phim 3x4 lap day phan duoi.
 * Ngang 320x240: cot trai (tieu de/nhac/cham), ban phim ben phai.
 * Nhap: cham "nay" (scale ~120 ms); du 4 so tu gui. Sai: cham do + rung ngang ~300 ms, 'Sai mã PIN, còn N lần'.
 * Khoa (5 sai lien tiep, 30 s, fd_pin): ban phim xam + 'Đã khóa, thử lại sau N giây' + thanh dem nguoc; tu mo lai.
 * Dung: cham xanh ~250 ms roi chuyen man.
 * Che do KHOA KHI THUC (ui_pin_wake_init luc khoi dong, NVS fd/pin_req): 'Nhập để mở khóa', khong nut huy;
 *   dung -> xoa co, vao Home; quay lai / Home / bang nhanh / nut deu bi nuot.
 * Che do QUAN LY (Cai dat): chua dat -> nhap 2 lan; da dat -> ma cu roi ma moi 2 lan (hoac 'Xoá mã PIN'); o thu 12 = huy. */
#include <stdio.h>
#include <string.h>
#include "fd_fonts.h"
#include "fd_pin.h"
#include "ui_priv.h"

typedef enum { ST_VERIFY, ST_NEW1, ST_NEW2, ST_DONE } st_t;

#define C_OK    0x16A34A
#define C_ERR   0xDC2626
#define C_TEXT  0x374151
#define DOT_SZ  20
#define K_CANCEL 11

static bool s_wake;   /* khoa sau khi thuc (fd/pin_req): chi mo khi nhap dung; destroy KHONG xoa */
static st_t s_st;
static char s_buf[4], s_first[4];
static int s_n;
static bool s_lock_shown, s_busy;   /* busy: dang rung / nhay xanh -> bo qua phim */
static lv_obj_t *s_prompt, *s_row, *s_dots[4], *s_kb, *s_clear, *s_bar;
static lv_timer_t *s_done_tmr;

static const char *const KMAP[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                   LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_CLOSE, ""};

bool ui_pin_wake_locked(void) { return s_wake; }

void ui_pin_wake_init(void)
{
    bool req = fd_pin_req_get();
    s_wake = req && fd_pin_is_set();
    if (req && !s_wake) fd_pin_req_set(false);   /* co co nhung khong con PIN -> bo co */
}

/* ---------- cham ---------- */
static void dot_color(lv_obj_t *d, uint32_t c, bool fill)
{
    lv_obj_set_style_border_color(d, lv_color_hex(c), 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(d, fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void dots_sync(void)
{
    for (int i = 0; i < 4; i++)
        if (s_dots[i]) dot_color(s_dots[i], FD_COLOR_PRIMARY, i < s_n);
}

static void scale_cb(void *o, int32_t v) { lv_obj_set_style_transform_scale((lv_obj_t *)o, v, 0); }

static void dot_pop(lv_obj_t *d)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, d);
    lv_anim_set_exec_cb(&a, scale_cb);
    lv_anim_set_values(&a, 256, 340);
    lv_anim_set_duration(&a, 60);
    lv_anim_set_reverse_duration(&a, 60);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void shake_cb(void *o, int32_t v)
{
    static const int8_t X[] = {0, -9, 9, -7, 7, -4, 4, 0};
    lv_obj_set_style_translate_x((lv_obj_t *)o, X[v < 0 ? 0 : v > 7 ? 7 : v], 0);
}

static void shake_done(lv_anim_t *a)
{
    (void)a;
    s_busy = false;
    s_n = 0;
    dots_sync();
}

static void dots_error(void)
{
    for (int i = 0; i < 4; i++) dot_color(s_dots[i], C_ERR, true);
    s_busy = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_row);
    lv_anim_set_exec_cb(&a, shake_cb);
    lv_anim_set_values(&a, 0, 7);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_completed_cb(&a, shake_done);
    lv_anim_start(&a);
}

/* ---------- nhac / khoa ---------- */
static void prompt(const char *t, bool err)
{
    lv_label_set_text(s_prompt, t);
    lv_obj_set_style_text_color(s_prompt, lv_color_hex(err ? C_ERR : C_TEXT), 0);
}

static void stage_prompt(void)
{
    if (s_st == ST_VERIFY) prompt(s_wake ? "Nhập để mở khóa" : "Nhập mã PIN hiện tại", false);
    else if (s_st == ST_NEW1) prompt("Nhập mã PIN mới", false);
    else if (s_st == ST_NEW2) prompt("Nhập lại mã PIN mới", false);
    if (s_clear) lv_obj_set_hidden(s_clear, !(s_st == ST_NEW1 && fd_pin_is_set() && !s_wake));
}

static void lock_prompt(int left)
{
    char t[48];
    snprintf(t, sizeof(t), "Đã khóa, thử lại sau %d giây", left);
    prompt(t, true);
    s_lock_shown = true;
    if (s_bar) {
        lv_obj_set_hidden(s_bar, false);
        lv_bar_set_value(s_bar, left, LV_ANIM_ON);
    }
    if (s_kb) lv_obj_add_state(s_kb, LV_STATE_DISABLED);   /* ban phim xam */
}

static void unlock_ui(void)
{
    s_lock_shown = false;
    if (s_bar) lv_obj_set_hidden(s_bar, true);
    if (s_kb) lv_obj_remove_state(s_kb, LV_STATE_DISABLED);
    s_n = 0;
    dots_sync();
    stage_prompt();
}

/* ---------- ket thuc ---------- */
static void done_tmr_cb(lv_timer_t *t)
{
    (void)t;
    s_done_tmr = NULL;   /* timer 1 lan: LVGL tu xoa */
    if (s_wake) {
        s_wake = false;
        fd_ui_show(FD_SCR_HOME);
    } else {
        ui_nav_back();
    }
}

static void success_then(const char *toast)
{
    s_st = ST_DONE;
    s_busy = true;
    for (int i = 0; i < 4; i++) dot_color(s_dots[i], C_OK, true);
    if (toast) fd_ui_toast(toast);
    if (s_clear) lv_obj_set_hidden(s_clear, true);
    s_done_tmr = lv_timer_create(done_tmr_cb, 250, NULL);
    lv_timer_set_repeat_count(s_done_tmr, 1);
}

static void submit(void)
{
    char d[4];
    memcpy(d, s_buf, 4);
    memset(s_buf, 0, sizeof(s_buf));
    if (s_st == ST_VERIFY) {
        int left = fd_pin_lock_left_s();
        if (left > 0) {
            s_n = 0;
            dots_sync();
            lock_prompt(left);
        } else if (fd_pin_verify(d)) {
            if (s_wake) {
                fd_pin_req_set(false);   /* dung: xoa co ngay (truoc khi nhay xanh) */
                success_then(NULL);
            } else {
                s_st = ST_NEW1;   /* ma cu dung -> nhap ma moi */
                for (int i = 0; i < 4; i++) dot_color(s_dots[i], C_OK, true);
                s_busy = true;
                lv_anim_t a;   /* nhay xanh ngan roi xoa cham */
                lv_anim_init(&a);
                lv_anim_set_var(&a, s_row);
                lv_anim_set_values(&a, 0, 0);
                lv_anim_set_duration(&a, 250);
                lv_anim_set_exec_cb(&a, shake_cb);
                lv_anim_set_completed_cb(&a, shake_done);
                lv_anim_start(&a);
                stage_prompt();
            }
        } else {
            left = fd_pin_lock_left_s();
            dots_error();
            if (left > 0) {
                lock_prompt(left);
            } else {
                char t[48];
                snprintf(t, sizeof(t), "Sai mã PIN, còn %d lần", fd_pin_fails_left());
                prompt(t, true);
            }
        }
    } else if (s_st == ST_NEW1) {
        memcpy(s_first, d, 4);
        s_st = ST_NEW2;
        s_n = 0;
        dots_sync();
        stage_prompt();
    } else if (s_st == ST_NEW2) {
        if (memcmp(s_first, d, 4) == 0) {
            bool ok = fd_pin_set(d);
            if (ok) success_then("Đã lưu mã PIN");
            else { prompt("Lỗi lưu mã PIN", true); dots_error(); s_st = ST_NEW1; }
        } else {
            s_st = ST_NEW1;
            prompt("Không khớp. Nhập mã PIN mới", true);
            dots_error();
        }
    }
    memset(d, 0, sizeof(d));
}

static void kb_cb(lv_event_t *e)
{
    (void)e;
    if (s_st == ST_DONE || s_lock_shown || s_busy) return;
    uint32_t id = lv_buttonmatrix_get_selected_button(s_kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    if (id == K_CANCEL) {
        if (!s_wake) ui_nav_back();   /* huy chi o che do Cai dat */
        return;
    }
    const char *t = lv_buttonmatrix_get_button_text(s_kb, id);
    if (!t) return;
    if (strcmp(t, LV_SYMBOL_BACKSPACE) == 0) {
        if (s_n > 0) {
            s_n--;
            s_buf[s_n] = 0;
            dots_sync();
        }
    } else if (t[0] >= '0' && t[0] <= '9' && s_n < 4) {
        s_buf[s_n] = t[0];
        dot_color(s_dots[s_n], FD_COLOR_PRIMARY, true);
        dot_pop(s_dots[s_n]);
        if (++s_n == 4) submit();   /* tu gui khi du 4 so */
    }
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    if (s_busy) return;
    bool ok = fd_pin_clear();
    if (ok) success_then("Đã xoá mã PIN");
    else prompt("Lỗi xoá mã PIN", true);
}

/* ---------- dung man ---------- */
static void build(lv_obj_t *scr)
{
    bool portrait = lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL);
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_flex_flow(body, portrait ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_style_pad_gap(body, 6, 0);

    lv_obj_t *info = lv_obj_create(body);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, portrait ? lv_pct(100) : 124, portrait ? LV_SIZE_CONTENT : lv_pct(100));
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info, portrait ? 6 : 10, 0);
    ui_screen_title(info, "Mã PIN");

    s_prompt = lv_label_create(info);
    lv_obj_set_width(s_prompt, lv_pct(100));
    lv_obj_set_style_text_font(s_prompt, &fd_font_noto_18, 0);
    lv_obj_set_style_text_align(s_prompt, LV_TEXT_ALIGN_CENTER, 0);

    s_row = lv_obj_create(info);
    lv_obj_remove_style_all(s_row);
    lv_obj_set_size(s_row, LV_SIZE_CONTENT, DOT_SZ + 12);   /* cho cham phong to khi nay */
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_row, portrait ? 18 : 12, 0);
    lv_obj_set_style_pad_hor(s_row, 6, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *d = lv_obj_create(s_row);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_SZ, DOT_SZ);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 2, 0);
        lv_obj_set_style_transform_pivot_x(d, DOT_SZ / 2, 0);
        lv_obj_set_style_transform_pivot_y(d, DOT_SZ / 2, 0);
        s_dots[i] = d;
    }

    s_bar = lv_bar_create(info);   /* dem nguoc khi khoa */
    lv_obj_set_size(s_bar, lv_pct(80), 6);
    lv_bar_set_range(s_bar, 0, 30);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(C_ERR), LV_PART_INDICATOR);
    lv_obj_set_hidden(s_bar, true);

    s_clear = lv_button_create(info);
    lv_obj_set_height(s_clear, 30);
    lv_obj_set_style_shadow_width(s_clear, 0, 0);
    lv_obj_set_style_bg_color(s_clear, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_clear, 1, 0);
    lv_obj_set_style_border_color(s_clear, lv_color_hex(C_ERR), 0);
    lv_obj_set_style_text_color(s_clear, lv_color_hex(C_ERR), 0);
    lv_obj_t *cl = lv_label_create(s_clear);
    lv_label_set_text(cl, "Xoá mã PIN");
    lv_obj_center(cl);
    lv_obj_add_event_cb(s_clear, clear_cb, LV_EVENT_CLICKED, NULL);

    s_kb = lv_buttonmatrix_create(body);
    lv_buttonmatrix_set_map(s_kb, KMAP);
    if (portrait) {
        lv_obj_set_size(s_kb, lv_pct(100), 0);
        lv_obj_set_flex_grow(s_kb, 1);
    } else {
        lv_obj_set_size(s_kb, 0, lv_pct(100));
        lv_obj_set_flex_grow(s_kb, 1);
    }
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 0, 0);
    lv_obj_set_style_pad_gap(s_kb, 6, 0);
    lv_obj_set_style_text_font(s_kb, &fd_font_mont_24, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(FD_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(FD_COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(FD_COLOR_PRIMARY_TINT), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(FD_COLOR_PRIMARY), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_opa(s_kb, LV_OPA_40, LV_STATE_DISABLED);
    if (s_wake) lv_buttonmatrix_set_button_ctrl(s_kb, K_CANCEL, LV_BUTTONMATRIX_CTRL_HIDDEN);   /* khong huy khi khoa */
    lv_obj_add_event_cb(s_kb, kb_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_n = 0;
    s_busy = false;
    memset(s_buf, 0, sizeof(s_buf));
    s_st = (s_wake || fd_pin_is_set()) ? ST_VERIFY : ST_NEW1;
    stage_prompt();
    dots_sync();
    int left_s = s_st == ST_VERIFY ? fd_pin_lock_left_s() : 0;
    if (left_s > 0) lock_prompt(left_s);
}

static void on_tick(void)
{
    if (s_st != ST_VERIFY) return;
    int left = fd_pin_lock_left_s();
    if (left > 0) lock_prompt(left);
    else if (s_lock_shown) unlock_ui();   /* het khoa */
}

static void destroy(void)
{
    if (s_row) lv_anim_delete(s_row, NULL);
    for (int i = 0; i < 4; i++)
        if (s_dots[i]) lv_anim_delete(s_dots[i], NULL);
    if (s_done_tmr) {
        lv_timer_delete(s_done_tmr);
        s_done_tmr = NULL;
    }
    s_prompt = s_row = s_kb = s_clear = s_bar = NULL;
    memset(s_dots, 0, sizeof(s_dots));
    memset(s_buf, 0, sizeof(s_buf));
    memset(s_first, 0, sizeof(s_first));
    s_n = 0;
    s_lock_shown = s_busy = false;
}

static bool on_button(const fd_evt_button_t *b) { return s_wake || !(b->index == 0 && b->action == FD_BTN_LONG_PRESS); }
static bool on_back(void) { return s_wake; }   /* dang khoa sau khi thuc: khong quay lai duoc */

const ui_screen_t g_scr_pin = {
    .name = "pin", .parent = FD_SCR_SETTINGS, .build = build, .destroy = destroy, .on_tick = on_tick,
    .on_button = on_button, .on_back = on_back,
};
