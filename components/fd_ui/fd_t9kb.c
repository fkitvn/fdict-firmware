/*
 * fd_t9kb.c - Ban phim T9 multi-tap cho Fdict (LVGL 9.6, 320x240).
 *
 * Bo cuc (grid 5 cot x 3 hang, giong ban phim dien thoai):
 *   [1 .@-_!?] [2 abc] [3 def] | [ ⌫ ] [QWE]   <- QWE: chuyen sang ban phim QWERTY (lv_keyboard)
 *   [4 ghi   ] [5 jkl] [6 mno] | [123] [ABC]   <- 123/abc toggle, mode (abc/ABC/#+=)
 *   [7 pqrs  ] [8 tuv] [9 wxyz]| [␣ 0] [ ✓ ]
 * Con tro: cham vao textarea de dat vi tri (cursor_click_pos).
 */
#include "fd_t9kb.h"
#include "ui_theme.h"
#include <string.h>
#include <stdlib.h>

/* ---------- Font (co the override khi build) ---------- */
#ifndef FD_T9KB_FONT_BIG
LV_FONT_DECLARE(fd_font_noto_18)
LV_FONT_DECLARE(fd_font_noto_14)
#define FD_T9KB_FONT_BIG   (&fd_font_noto_18)
#define FD_T9KB_FONT_SMALL (&fd_font_noto_14)
#endif
#ifndef FD_T9KB_FONT_SYM
#define FD_T9KB_FONT_SYM   (&lv_font_montserrat_14)   /* co glyph LV_SYMBOL_* */
#endif

#define T9_DEF_TIMEOUT_MS   900
#define T9_CLEAR_HOLD_MS    2000   /* giu ⌫ >= 2s -> xoa het */
#define T9_GAP              4

enum {
    K_L0 = 0, /* 9 phim ky tu K_L0..K_L8 = phim so 1..9 */
    K_SPACE = 9,
    K_BKSP, K_MODE, K_NUM, K_DONE, K_QW,
    K_COUNT
};

/* vi tri (cot, hang) */
static const uint8_t k_pos[K_COUNT][2] = {
    {0,0},{1,0},{2,0}, {0,1},{1,1},{2,1}, {0,2},{1,2},{2,2},
    {3,2}, /* space */
    {3,0}, /* bksp */
    {4,1}, /* mode: abc/ABC/#+= */
    {3,1}, /* 123 <-> abc */
    {4,2}, /* done  */
    {4,0}, /* QWERTY */
};

/* ---------- QWERTY (lv_keyboard) map: phim "T9" de quay lai ----------
 * Chi dung ASCII + LV_SYMBOL_* (font Montserrat mac dinh cua lv_keyboard). */
#define QW_T9 "T9"
#define QW_BTN(w) (LV_BUTTONMATRIX_CTRL_POPOVER | (w))
static const char *const qw_map_lc[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "@", "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    QW_T9, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const char *const qw_map_uc[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "@", "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    QW_T9, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const char *const qw_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    QW_T9, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
#define QW_CTRL LV_KEYBOARD_CTRL_BUTTON_FLAGS
#define QW_CHK  LV_BUTTONMATRIX_CTRL_CHECKED
static const lv_buttonmatrix_ctrl_t qw_ctrl_txt[] = {
    QW_CTRL | 5, QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_BTN(4), QW_CHK | 7,
    QW_CTRL | 6, QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3), QW_BTN(3),
    QW_CHK | QW_BTN(1), QW_CHK | QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_CHK | QW_BTN(1), QW_CHK | QW_BTN(1), QW_CHK | QW_BTN(1),
    QW_CTRL | 2, QW_CHK | 2, 6, QW_CHK | 2, QW_CTRL | 2
};
static const lv_buttonmatrix_ctrl_t qw_ctrl_spec[] = {
    QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_CHK | 2,
    QW_CTRL | 2, QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1),
    QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1), QW_BTN(1),
    QW_CTRL | 2, QW_CHK | 2, 6, QW_CHK | 2, QW_CTRL | 2
};

/* bang ky tu: [mode][K_L0..K_SPACE]. Thu tu = thu tu khi bam lap. */
static const char *const k_chars[FD_T9KB_MODE_COUNT][10] = {
    [FD_T9KB_MODE_LOWER]  = { ".@-_!?1", "abc2", "def3", "ghi4", "jkl5", "mno6", "pqrs7", "tuv8", "wxyz9", " 0" },
    [FD_T9KB_MODE_UPPER]  = { ".@-_!?1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9", " 0" },
    [FD_T9KB_MODE_DIGIT]  = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },  /* chi so, khong cycle */
    /* 8 nhom dau tien phu du 32 ky tu dau ASCII; nhom 9 lap lai cac ky tu hay dung */
    [FD_T9KB_MODE_SYMBOL] = { ".,?!", "@#$%", "&*+-", "=_/\\", "()[]", "{}<>", ":;'\"", "^~`|", "-_.@", " " },
};
static const char *const k_mode_name[FD_T9KB_MODE_COUNT] = { "abc", "ABC", "123", "#+=" };

typedef struct {
    lv_obj_t *kb;
    lv_obj_t *grid;      /* luoi phim T9 */
    lv_obj_t *qwerty;    /* lv_keyboard (tao khi can, an khi quay ve T9) */
    fd_t9kb_layout_t layout;
    lv_obj_t *ta;
    lv_obj_t *btn[K_COUNT];
    lv_obj_t *lbl_main[10];
    lv_obj_t *lbl_hint[10];
    lv_obj_t *lbl_mode;
    lv_obj_t *lbl_num;
    lv_timer_t *timer;
    fd_t9kb_done_cb_t on_done;
    void *user_data;
    fd_t9kb_mode_t mode;
    fd_t9kb_mode_t prev_mode;   /* che do chu/ky hieu truoc khi vao 123 */
    uint32_t timeout;
    int8_t pend_key;     /* -1: khong co ky tu dang cho */
    uint8_t pend_idx;
    
    uint32_t bksp_t0;
} t9_ctx_t;

static fd_t9kb_mode_t next_letter_mode(fd_t9kb_mode_t m)
{
    switch (m) {
    case FD_T9KB_MODE_LOWER: return FD_T9KB_MODE_UPPER;
    case FD_T9KB_MODE_UPPER: return FD_T9KB_MODE_SYMBOL;
    default:                 return FD_T9KB_MODE_LOWER;
    }
}

static t9_ctx_t *ctx_of(lv_obj_t *kb) { return kb ? (t9_ctx_t *)lv_obj_get_user_data(kb) : NULL; }

/* ---------- textarea helpers ---------- */
static void sel_clear(t9_ctx_t *c)
{
    if (!c->ta) return;
    lv_obj_t *l = lv_textarea_get_label(c->ta);
    lv_label_set_text_selection_start(l, LV_LABEL_TEXT_SELECTION_OFF);
    lv_label_set_text_selection_end(l, LV_LABEL_TEXT_SELECTION_OFF);
}

static void sel_last(t9_ctx_t *c)
{
    uint32_t pos = lv_textarea_get_cursor_pos(c->ta);
    if (pos == 0) return;
    lv_obj_t *l = lv_textarea_get_label(c->ta);
    lv_label_set_text_selection_start(l, pos - 1);
    lv_label_set_text_selection_end(l, pos);
}

static bool ta_full(t9_ctx_t *c)
{
    uint32_t max = lv_textarea_get_max_length(c->ta);
    return max && strlen(lv_textarea_get_text(c->ta)) >= max;
}

static void commit(t9_ctx_t *c)
{
    if (c->pend_key < 0) return;
    lv_obj_set_state(c->btn[c->pend_key], LV_STATE_CHECKED, false);
    c->pend_key = -1;
    c->pend_idx = 0;
    lv_timer_pause(c->timer);
    sel_clear(c);
}

static void timer_cb(lv_timer_t *t) { commit((t9_ctx_t *)lv_timer_get_user_data(t)); }

/* chen 1 ky tu (ASCII) */
static void put_char(t9_ctx_t *c, char ch)
{
    lv_textarea_add_char(c->ta, (uint32_t)(uint8_t)ch);
}

static void tap_char_key(t9_ctx_t *c, int k)
{
    if (!c->ta) return;
    const char *set = k_chars[c->mode][k];
    size_t n = strlen(set);

    if (c->pend_key == k) {                  /* bam lap: doi ky tu dang cho */
        c->pend_idx = (uint8_t)((c->pend_idx + 1) % n);
        lv_textarea_delete_char(c->ta);
        put_char(c, set[c->pend_idx]);
        sel_last(c);
        lv_timer_reset(c->timer);
        return;
    }
    commit(c);
    if (ta_full(c)) return;                  /* dat gioi han (63 voi WPA2) */
    put_char(c, set[0]);
    if (n == 1) return;                      /* 1 ky tu: chot ngay */
    c->pend_key = (int8_t)k;
    c->pend_idx = 0;
    lv_obj_set_state(c->btn[k], LV_STATE_CHECKED, true);
    sel_last(c);
    lv_timer_reset(c->timer);
    lv_timer_resume(c->timer);
}

/* long-press phim ky tu: chen thang ky tu cuoi cua nhom (chu so o che do chu) */
static void long_char_key(t9_ctx_t *c, int k)
{
    if (!c->ta) return;
    commit(c);
    if (ta_full(c)) return;
    const char *set = k_chars[c->mode][k];
    put_char(c, set[strlen(set) - 1]);
}

/* ---------- nhan phim ---------- */
static void relabel(t9_ctx_t *c)
{
    for (int k = 0; k <= K_SPACE; k++) {
        const char *set = k_chars[c->mode][k];
        char main[12] = {0}, hint[4] = {0};
        size_t n = strlen(set);
        if (k == K_SPACE) {
            strcpy(hint, c->mode == FD_T9KB_MODE_SYMBOL ? "" : "0");
            strcpy(main, "space");
            if (c->mode == FD_T9KB_MODE_DIGIT) { strcpy(main, "0"); hint[0] = 0; }
        } else if (c->mode == FD_T9KB_MODE_LOWER || c->mode == FD_T9KB_MODE_UPPER) {
            hint[0] = set[n - 1];               /* chu so */
            memcpy(main, set, n - 1);           /* abc */
        } else {
            memcpy(main, set, n < sizeof(main) ? n : sizeof(main) - 1);
        }
        lv_label_set_text(c->lbl_main[k], main);
        lv_obj_set_style_text_font(c->lbl_main[k],
            strlen(main) > 4 ? FD_T9KB_FONT_SMALL : FD_T9KB_FONT_BIG, 0);
        lv_label_set_text(c->lbl_hint[k], hint);
    }
    bool dig = c->mode == FD_T9KB_MODE_DIGIT;
    lv_label_set_text(c->lbl_num, dig ? "abc" : "123");
    /* phim mode hien che do se chuyen toi khi bam */
    fd_t9kb_mode_t base = dig ? c->prev_mode : c->mode;
    lv_label_set_text(c->lbl_mode, k_mode_name[next_letter_mode(base)]);
}

/* ---------- su kien ---------- */
static void key_event_cb(lv_event_t *e)
{
    t9_ctx_t *c = (t9_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    int k = (int)(intptr_t)lv_obj_get_user_data(b);
    lv_event_code_t code = lv_event_get_code(e);

    if (k <= K_SPACE) {
        if (code == LV_EVENT_SHORT_CLICKED) tap_char_key(c, k);
        else if (code == LV_EVENT_LONG_PRESSED && k != K_SPACE) {
            
            long_char_key(c, k);
        }
        return;
    }
    switch (k) {
    case K_BKSP:
        if (code == LV_EVENT_PRESSED) c->bksp_t0 = lv_tick_get();
        else if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED) fd_t9kb_backspace(c->kb);
        else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
            if (lv_tick_elaps(c->bksp_t0) >= T9_CLEAR_HOLD_MS) fd_t9kb_clear(c->kb);
            else fd_t9kb_backspace(c->kb);
        }
        break;
    case K_MODE:
        if (code == LV_EVENT_SHORT_CLICKED) fd_t9kb_next_mode(c->kb);
        else if (code == LV_EVENT_LONG_PRESSED) fd_t9kb_set_mode(c->kb, FD_T9KB_MODE_LOWER);
        break;
    case K_NUM:
        if (code == LV_EVENT_SHORT_CLICKED) fd_t9kb_toggle_digits(c->kb);
        break;
    case K_QW:
        if (code == LV_EVENT_SHORT_CLICKED) fd_t9kb_set_layout(c->kb, FD_T9KB_LAYOUT_QWERTY);
        break;
    case K_DONE:
        if (code == LV_EVENT_SHORT_CLICKED) fd_t9kb_done(c->kb);
        break;
    }
}

static void ta_delete_cb(lv_event_t *e)
{
    t9_ctx_t *c = (t9_ctx_t *)lv_event_get_user_data(e);
    c->ta = NULL;
    if (c->qwerty) lv_keyboard_set_textarea(c->qwerty, NULL);
    c->pend_key = -1;
    if (c->timer) lv_timer_pause(c->timer);
}

static void kb_delete_cb(lv_event_t *e)
{
    t9_ctx_t *c = (t9_ctx_t *)lv_event_get_user_data(e);
    if (c->timer) lv_timer_delete(c->timer);
    if (c->ta) lv_obj_remove_event_cb_with_user_data(c->ta, ta_delete_cb, c);
    lv_obj_set_user_data(c->kb, NULL);
    free(c);
}

/* ---------- QWERTY ---------- */
/* Thay lv_keyboard_def_event_cb: chan "T9" va OK, con lai giao cho handler mac dinh. */
static void qw_event_cb(lv_event_t *e)
{
    t9_ctx_t *c = (t9_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *k = lv_event_get_current_target(e);
    uint32_t id = lv_keyboard_get_selected_button(k);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *txt = lv_keyboard_get_button_text(k, id);
    if (!txt) return;
    if (strcmp(txt, QW_T9) == 0) { fd_t9kb_set_layout(c->kb, FD_T9KB_LAYOUT_T9); return; }
    if (strcmp(txt, LV_SYMBOL_OK) == 0) { fd_t9kb_done(c->kb); return; }
    lv_keyboard_def_event_cb(e);
}

static void qw_create(t9_ctx_t *c)
{
    lv_obj_t *k = lv_keyboard_create(c->kb);
    lv_obj_set_size(k, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(k, 2, 0);
    lv_obj_set_style_pad_gap(k, 3, 0);
    lv_obj_set_style_text_font(k, FD_T9KB_FONT_SYM, LV_PART_ITEMS);
    lv_keyboard_set_map(k, LV_KEYBOARD_MODE_TEXT_LOWER, qw_map_lc, qw_ctrl_txt);
    lv_keyboard_set_map(k, LV_KEYBOARD_MODE_TEXT_UPPER, qw_map_uc, qw_ctrl_txt);
    lv_keyboard_set_map(k, LV_KEYBOARD_MODE_SPECIAL, qw_map_spec, qw_ctrl_spec);
    lv_keyboard_set_mode(k, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_event_cb(k, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(k, qw_event_cb, LV_EVENT_VALUE_CHANGED, c);
    c->qwerty = k;
}

void fd_t9kb_set_layout(lv_obj_t *kb, fd_t9kb_layout_t layout)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c || layout == c->layout) return;
    commit(c);
    c->layout = layout;
    if (layout == FD_T9KB_LAYOUT_QWERTY) {
        if (!c->qwerty) qw_create(c);
        lv_keyboard_set_textarea(c->qwerty, c->ta);
        lv_obj_set_hidden(c->grid, true);
        lv_obj_set_hidden(c->qwerty, false);
    } else {
        if (c->qwerty) {
            lv_keyboard_set_textarea(c->qwerty, NULL);
            lv_obj_set_hidden(c->qwerty, true);
        }
        lv_obj_set_hidden(c->grid, false);
    }
}

fd_t9kb_layout_t fd_t9kb_get_layout(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    return c ? c->layout : FD_T9KB_LAYOUT_T9;
}

/* ---------- tao ---------- */
static lv_obj_t *mk_key(t9_ctx_t *c, int k)
{
    lv_obj_t *b = lv_button_create(c->grid);
    lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, k_pos[k][0], 1, LV_GRID_ALIGN_STRETCH, k_pos[k][1], 1);
    lv_obj_set_user_data(b, (void *)(intptr_t)k);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xBFDBFE), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xFDE68A), LV_STATE_CHECKED);   /* phim dang cho */
    lv_obj_set_style_border_color(b, lv_color_hex(0xF59E0B), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(b, 2, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, lv_color_hex(0x1F2937), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, lv_color_hex(0x1F2937), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, key_event_cb, LV_EVENT_PRESSED, c);
    lv_obj_add_event_cb(b, key_event_cb, LV_EVENT_SHORT_CLICKED, c);
    lv_obj_add_event_cb(b, key_event_cb, LV_EVENT_LONG_PRESSED, c);
    lv_obj_add_event_cb(b, key_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, c);
    c->btn[k] = b;
    return b;
}

static lv_obj_t *mk_sym_label(lv_obj_t *b, const char *sym)
{
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, FD_T9KB_FONT_SYM, 0);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    return l;
}

lv_obj_t *fd_t9kb_create(lv_obj_t *parent, lv_obj_t *textarea,
                         fd_t9kb_done_cb_t on_done, void *user_data)
{
    static const int32_t cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const int32_t rows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };

    t9_ctx_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->on_done = on_done;
    c->user_data = user_data;
    c->timeout = T9_DEF_TIMEOUT_MS;
    c->pend_key = -1;

    lv_obj_t *kb = lv_obj_create(parent);
    c->kb = kb;
    lv_obj_remove_style_all(kb);
    lv_obj_set_size(kb, lv_pct(100), FD_T9KB_DEF_HEIGHT);
    lv_obj_set_scrollable(kb, false);
    lv_obj_set_user_data(kb, c);

    c->grid = lv_obj_create(kb);
    lv_obj_remove_style_all(c->grid);
    lv_obj_set_size(c->grid, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollable(c->grid, false);
    lv_obj_set_style_pad_row(c->grid, T9_GAP, 0);
    lv_obj_set_style_pad_column(c->grid, T9_GAP, 0);
    lv_obj_set_grid_dsc_array(c->grid, cols, rows);
    lv_obj_add_event_cb(kb, kb_delete_cb, LV_EVENT_DELETE, c);

    for (int k = 0; k <= K_SPACE; k++) {
        lv_obj_t *b = mk_key(c, k);
        c->lbl_main[k] = lv_label_create(b);
        lv_obj_align(c->lbl_main[k], LV_ALIGN_CENTER, 0, 4);
        c->lbl_hint[k] = lv_label_create(b);
        lv_obj_set_style_text_font(c->lbl_hint[k], FD_T9KB_FONT_SMALL, 0);
        lv_obj_set_style_text_color(c->lbl_hint[k], lv_color_hex(0x64748B), 0);
        lv_obj_align(c->lbl_hint[k], LV_ALIGN_TOP_LEFT, 4, 1);
    }

    lv_obj_t *b;
    b = mk_key(c, K_BKSP);  mk_sym_label(b, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xE2E8F0), 0);
    b = mk_key(c, K_MODE);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xE2E8F0), 0);
    c->lbl_mode = lv_label_create(b);
    lv_obj_set_style_text_font(c->lbl_mode, FD_T9KB_FONT_BIG, 0);
    lv_obj_center(c->lbl_mode);
    b = mk_key(c, K_NUM);   /* 123 <-> abc: noi bat de de thay */
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY_TINT), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    c->lbl_num = lv_label_create(b);
    lv_obj_set_style_text_font(c->lbl_num, FD_T9KB_FONT_BIG, 0);
    lv_obj_center(c->lbl_num);
    b = mk_key(c, K_QW);    /* chuyen sang QWERTY */
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY_TINT), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(FD_COLOR_PRIMARY_TEXT), 0);
    {
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, FD_T9KB_FONT_BIG, 0);
        lv_label_set_text(l, "QWE");
        lv_obj_center(l);
    }
    b = mk_key(c, K_DONE);  mk_sym_label(b, LV_SYMBOL_OK);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_obj_set_style_border_width(b, 0, 0);

    c->timer = lv_timer_create(timer_cb, c->timeout, c);
    lv_timer_pause(c->timer);

    relabel(c);
    fd_t9kb_set_textarea(kb, textarea);
    return kb;
}

void fd_t9kb_set_textarea(lv_obj_t *kb, lv_obj_t *ta)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c) return;
    commit(c);
    if (c->ta) lv_obj_remove_event_cb_with_user_data(c->ta, ta_delete_cb, c);
    c->ta = ta;
    if (c->qwerty) lv_keyboard_set_textarea(c->qwerty, c->layout == FD_T9KB_LAYOUT_QWERTY ? ta : NULL);
    if (!ta) return;
    lv_obj_add_event_cb(ta, ta_delete_cb, LV_EVENT_DELETE, c);
    /* ky tu dang cho: nen vang tren label cua textarea */
    lv_obj_t *l = lv_textarea_get_label(ta);
    lv_obj_set_style_bg_color(l, lv_color_hex(0xFDE68A), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(l, lv_color_hex(0x111827), LV_PART_SELECTED);
    /* che do mat khau: hien ky tu cuoi dung bang thoi gian cho commit */
    lv_textarea_set_password_show_time(ta, c->timeout + 300);
}

void fd_t9kb_set_mode(lv_obj_t *kb, fd_t9kb_mode_t mode)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c || mode >= FD_T9KB_MODE_COUNT) return;
    commit(c);
    if (mode == FD_T9KB_MODE_DIGIT && c->mode != FD_T9KB_MODE_DIGIT) c->prev_mode = c->mode;
    c->mode = mode;
    relabel(c);
}

fd_t9kb_mode_t fd_t9kb_get_mode(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    return c ? c->mode : FD_T9KB_MODE_LOWER;
}

void fd_t9kb_next_mode(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c) return;
    fd_t9kb_mode_t base = c->mode == FD_T9KB_MODE_DIGIT ? c->prev_mode : c->mode;
    fd_t9kb_set_mode(kb, next_letter_mode(base));
}

void fd_t9kb_toggle_digits(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c) return;
    fd_t9kb_set_mode(kb, c->mode == FD_T9KB_MODE_DIGIT ? c->prev_mode : FD_T9KB_MODE_DIGIT);
}

void fd_t9kb_set_timeout(lv_obj_t *kb, uint32_t ms)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c || ms < 200) return;
    c->timeout = ms;
    lv_timer_set_period(c->timer, ms);
    if (c->ta) lv_textarea_set_password_show_time(c->ta, ms + 300);
}

void fd_t9kb_commit(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    if (c) commit(c);
}

void fd_t9kb_backspace(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c || !c->ta) return;
    commit(c);            /* xoa ky tu dang cho cung la xoa 1 ky tu */
    lv_textarea_delete_char(c->ta);
}

void fd_t9kb_clear(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c || !c->ta) return;
    commit(c);
    lv_textarea_set_text(c->ta, "");
}

void fd_t9kb_done(lv_obj_t *kb)
{
    t9_ctx_t *c = ctx_of(kb);
    if (!c) return;
    commit(c);
    const char *txt = c->ta ? lv_textarea_get_text(c->ta) : "";
    if (c->ta) lv_obj_send_event(c->ta, LV_EVENT_READY, NULL);
    if (c->on_done) c->on_done(kb, txt, c->user_data);  /* co the xoa kb ben trong cb */
}
