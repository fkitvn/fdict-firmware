/* Home (khong header co dinh, 320x240 ngang; 14:36 19/10: dong trang thai luon hien):
 *            [logo FD]  [wifi]  [pin %]          (hang tren can giua, theo luong trang, cao 28 px)
 *        (dong trang thai can giua: chu + 👌)    (Noto 14 = 20 px; cham -> man WiFi tru khi READY)
 *   luoi 3 cot (Fluent Emoji 3D 48px + nhan Noto 14):
 *     Lat tu     | Luyen tap | Phan loai tu
 *     Dien tu    | Sap xep   | Nghe
 *     Tu dien    | Dong bo   | Thong ke
 *     Cai dat
 * 20/10: trang Home CUON DOC (khong con vua khit 228 px).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fd_batt.h"
#include "fd_fonts.h"
#include "esp_log.h"
#include "fd_deck.h"
#include "fd_net.h"
#include "fd_ota.h"
#include "fd_sync.h"
#include "ui_priv.h"

static lv_obj_t *s_stat;   /* hang trang thai (chu + icon): vung cham + ext 10 px, clickable tru khi READY */
static lv_obj_t *s_hint;   /* chu trang thai */
static lv_obj_t *s_ok;     /* emoji 👌 (anh 16x16), chi hien khi READY */
static lv_obj_t *s_wifi;
static lv_obj_t *s_batt;

LV_IMAGE_DECLARE(img_logo);   /* mark FD 28x28 (img_logo.c, dung chung bang nhanh) */
LV_IMAGE_DECLARE(img_ok_hand);   /* U+1F44C 16x16 (img_ok_hand.c, Noto Emoji Apache-2.0) - font khong co glyph emoji */

/* Icon o: Microsoft Fluent Emoji 3D (MIT) 48x48 RGB565A8, sinh boi tools/icons/gen_icons.py */
LV_IMAGE_DECLARE(img_tile_flashcard);
LV_IMAGE_DECLARE(img_tile_study);
LV_IMAGE_DECLARE(img_tile_sync);
LV_IMAGE_DECLARE(img_tile_stats);
LV_IMAGE_DECLARE(img_tile_settings);
LV_IMAGE_DECLARE(img_tile_sentence);   /* 2.1.7 Fluent "Puzzle piece" */
LV_IMAGE_DECLARE(img_tile_wordtype);   /* 2.3.6 Fluent "Label" - Phan loai tu */
LV_IMAGE_DECLARE(img_tile_cloze);      /* 2.3.6 Fluent "Pencil" - Dien tu */
LV_IMAGE_DECLARE(img_tile_dict);       /* 2.3.6 Fluent "Open book" - Tu dien */
LV_IMAGE_DECLARE(img_tile_listen);     /* Fluent "Speaker high volume" - Nghe */

static const struct {
    const lv_image_dsc_t *icon;
    const char *label;
    fd_scr_t target;
} TILES[] = {
    {&img_tile_flashcard, "FLASH_CARD", FD_SCR_FLASHCARD},
    {&img_tile_study, "PRACTICE", FD_SCR_PRACTICE},
    {&img_tile_wordtype, "WORD_TYPE", FD_SCR_POS},
    {&img_tile_cloze, "FILL_BLANK", FD_SCR_CLOZE},
    {&img_tile_sentence, "SENTENCE", FD_SCR_SENTENCE},
    {&img_tile_listen, "LISTEN", FD_SCR_LISTEN},
    {&img_tile_dict, "DICTIONARY", FD_SCR_DICT},
    {&img_tile_sync, "SYNC", FD_SCR_SYNC},
    {&img_tile_stats, "STATISTICS", FD_SCR_STATS},
    {&img_tile_settings, "SETTINGS", FD_SCR_SETTINGS},
};

static void go_cb(lv_event_t *e)
{
    fd_ui_show((fd_scr_t)(intptr_t)lv_event_get_user_data(e));
}

static void refresh(void)
{
    lv_obj_set_style_text_color(s_wifi, ui_wifi_color(), 0);
    ui_wifi_slash_sync(s_wifi, !fd_net_is_connected() && fd_net_state() != FD_NET_CONNECTING);   /* chua ket noi -> wifi gach cheo */
    ui_batt_label_update(s_batt);
    /* Trang thai: READY (da ket noi WiFi, khong xet gio SNTP) > dang ket noi > chua co mang luu > mat WiFi */
    /* 23/10: them trang thai ghep thiet bi (fd_ota_is_paired) + dong bo (fd_deck: outbox cho gui / bo tu da luu) */
    const char *txt;
    lv_color_t col = lv_color_hex(FD_COLOR_ALERT);
    bool tap = true, ready = false;
    fd_deck_status_t dk;
    fd_deck_get(&dk);   /* chi doc trang thai co san, khong goi mang (dk.outbox = ket qua cho gui) */
    /* 4.1.10: so tu offline lay tu KHO TU DONG BO fd_sync (ss.words = so tu dang nap trong kho tren may, cap nhat
     * ngay khi dong bo xong / xoa kho). Truoc day dung fd_deck.have (bo cu, khong con duoc tai) -> luon bao chua co du lieu. */
    fd_sync_status_t ss;
    fd_sync_get(&ss);   /* doc duoi mutex fd_sync; refresh chay trong task LVGL (on_tick 1 s + on_event + build) */
    bool have_words = ss.have && ss.words > 0;
    static int s_logged_words = -1;
    if (ss.words != s_logged_words) { s_logged_words = ss.words; ESP_LOGI("scr_home", "kho tu dong bo: %d tu -> %s", ss.words, have_words ? "san sang hoc offline" : "chua co du lieu offline"); }
    char syn[48];
    if (dk.outbox > 0) snprintf(syn, sizeof(syn), _("SYNC_PENDING_N"), dk.outbox);
    else snprintf(syn, sizeof(syn), "%s", have_words ? _("OFFLINE_DATA_READY") : _("NO_OFFLINE_DATA_YET"));
    static char buf[96];
    /* 4.1.10: Home KHONG hien gio dong bo nua. Co tu trong kho (>0) -> chi 'San sang de hoc ngoai tuyen' (ca khi co
     * hay mat WiFi; trang thai WiFi van xem o icon WiFi tren thanh trang thai). 0 tu: co WiFi -> 'Chua co du lieu offline',
     * mat WiFi -> chu trang thai WiFi (cham de mo WiFi). Ket qua cho gui (fd_deck outbox) van uu tien hien 'Cho dong bo N'. */
    if (have_words) {
        txt = syn;
        col = lv_palette_darken(LV_PALETTE_GREEN, 2);
        tap = !fd_net_is_connected();   /* mat WiFi: van cham de mo man WiFi */
        ready = fd_net_is_connected() && fd_ota_is_paired() && dk.outbox == 0;
    } else if (fd_net_is_connected()) {
        bool paired = fd_ota_is_paired();
        if (paired) snprintf(buf, sizeof(buf), "%s", syn);
        else snprintf(buf, sizeof(buf), "%s · %s", _("NOT_PAIRED"), syn);
        txt = buf;
        if (paired) col = lv_palette_darken(LV_PALETTE_GREEN, 2);
        tap = false;
        ready = paired && dk.outbox == 0;
    } else {
        const char *w = fd_net_state() == FD_NET_CONNECTING ? _("CONNECTING_TO_WIFI")
                      : !fd_net_saved_count() ? _("TAP_TO_SETUP_WIFI")
                      : _("WIFI_LOST_TAP_TO_OPEN");
        if (dk.outbox > 0) { snprintf(buf, sizeof(buf), "%s · %s", w, syn); txt = buf; }
        else txt = w;
    }
    if (strcmp(lv_label_get_text(s_hint), txt) != 0) { lv_label_set_text(s_hint, txt); ESP_LOGD("scr_home", "trang thai: %s", txt); }
    lv_obj_set_style_text_color(s_hint, col, 0);
    lv_obj_set_hidden(s_ok, !ready);
    lv_obj_set_clickable(s_stat, tap);   /* cham duoc tru khi READY */
}

/* Luoi o: kich thuoc px co dinh (khong dung %): 2 hang x TILE_H + khe TILE_GAP_ROW */
#define TILE_H        76
#define TILE_H_PORT   84   /* doc 240: icon 48 + 2 dong Noto 14 (20 px, line_space -4), pad 0 */
#define TILE_GAP_ROW_PORT 0   /* doc: 28 + 4 + 20 + 4 + 3 x 84 = 308 px = 320 - 2 x 6, khong cuon */
#define TILE_GAP_ROW  16
#define TILE_GAP_COL  8
#define BODY_PAD      6   /* = pad cua ui_page_create */

static void tile(lv_obj_t *grid, int i, int32_t tile_w)
{
    lv_obj_t *b = lv_button_create(grid);
    /* O phang: bo het style theme (nen amber, shadow, outline focus, grow/recolor khi nhan),
     * icon 3D nam truc tiep tren nen man. Chi giu vet nhan: nen bo goc amber nhat. */
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, tile_w, TILE_H_PORT);   /* 4.1.6: ca ngang cung cao 2 dong ten */
    lv_obj_set_style_radius(b, 12, LV_PART_MAIN | LV_STATE_ANY);   /* bo goc 12 px moi trang thai, ca khi nhan (khoi phuc 18/10 10:54) */
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_outline_width(b, 0, LV_PART_MAIN | LV_STATE_ANY);
    lv_obj_set_style_bg_color(b, lv_color_hex(FD_COLOR_PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);   /* bo le doc: icon 48 + 2 dong ten */
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 0, 0);
    lv_obj_t *ic = lv_image_create(b);   /* icon 3D 48x48: 2 + 48 + 2 + nhan Noto 14 vua TILE_H */
    lv_image_set_src(ic, TILES[i].icon);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &fd_font_noto_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x111827), 0);
    lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);   /* 4.1.6: ca doc lan ngang: xuong dong o khoang trang, toi da 2 dong can giua (khong cat "..." / giua tu) */
    lv_obj_set_style_text_line_space(l, -4, 0);   /* 2 dong ten sat hon */
    lv_label_set_text(l, _(TILES[i].label));
    lv_obj_add_event_cb(b, go_cb, LV_EVENT_CLICKED, (void *)(intptr_t)TILES[i].target);
}

static void build(lv_obj_t *scr)
{
    lv_obj_t *body = ui_page_create(scr);
    lv_obj_set_style_pad_row(body, (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? 4 : 6, 0);   /* ngang: 28 + 6 + 20 + 6 + 168 = 228 px; doc: khe 4 */
    lv_obj_set_scrollable(body, true);   /* 20/10: Home cuon doc (luoi 3 hang) */
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    /* Hang tren (theo luong trang, khong co dinh): logo | wifi | pin, can giua */
    lv_obj_t *top = lv_obj_create(body);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 10, 0);
    lv_obj_set_scrollable(top, false);
    lv_obj_set_clickable(top, false);
    lv_obj_t *logo = lv_image_create(top);
    lv_image_set_src(logo, &img_logo);
    s_wifi = lv_label_create(top);
    lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_clickable(s_wifi, true);
    lv_obj_set_ext_click_area(s_wifi, 10);
    lv_obj_add_event_cb(s_wifi, go_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FD_SCR_WIFI);
    s_batt = ui_batt_label_create(top);

    /* Dong trang thai luon hien, can giua ngay duoi hang tren: [chu][👌]. Ca hang (rong 100%) la vung cham + ext 10 px. */
    s_stat = lv_obj_create(body);
    lv_obj_remove_style_all(s_stat);
    lv_obj_set_size(s_stat, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_stat, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_stat, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_stat, 4, 0);
    lv_obj_set_scrollable(s_stat, false);
    lv_obj_set_ext_click_area(s_stat, 10);
    lv_obj_add_event_cb(s_stat, go_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FD_SCR_WIFI);   /* clickable bat/tat trong refresh */
    s_hint = lv_label_create(s_stat);
    lv_label_set_text(s_hint, "");
    if ((lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL))) {   /* doc: 1 dong 20 px (ngan sach 320 px khit), du cat "..." chua anh 16 px + khe 4 */
        lv_obj_set_width(s_hint, lv_display_get_horizontal_resolution(NULL) - 2 * BODY_PAD - 20);
        lv_label_set_long_mode(s_hint, LV_LABEL_LONG_DOT);
    } else {
        lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_max_width(s_hint, lv_pct(100), 0);
    }
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_clickable(s_hint, false);   /* cham roi vao hang cha */
    s_ok = lv_image_create(s_stat);
    lv_image_set_src(s_ok, &img_ok_hand);
    lv_obj_set_clickable(s_ok, false);

    /* Luoi 3x2, kich thuoc px tinh tu do phan giai: khe hang/cot co dinh, can START */
    int32_t gw = lv_display_get_horizontal_resolution(NULL) - 2 * BODY_PAD;
    int32_t tile_w = (gw - 2 * TILE_GAP_COL) / 3;
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    const int n_tiles = (int)(sizeof(TILES) / sizeof(TILES[0])), rows = (n_tiles + 2) / 3;
    int32_t th = TILE_H_PORT;   /* 4.1.6: ngang cung 2 dong ten */
    lv_obj_set_size(grid, 3 * tile_w + 2 * TILE_GAP_COL, rows * th + (rows - 1) * ((lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? TILE_GAP_ROW_PORT : TILE_GAP_ROW));
    lv_obj_set_scrollable(grid, false);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER,   /* 5 o: hang 2 can giua */ LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, (lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL)) ? TILE_GAP_ROW_PORT : TILE_GAP_ROW, 0);
    lv_obj_set_style_pad_column(grid, TILE_GAP_COL, 0);
    for (int i = 0; i < (int)(sizeof(TILES) / sizeof(TILES[0])); i++) tile(grid, i, tile_w);
    refresh();
}

static void destroy(void) { s_stat = s_hint = s_ok = s_wifi = s_batt = NULL; }
static void on_event(int32_t id, void *data) { (void)id; (void)data; refresh(); }
static void on_tick(void) { refresh(); }

const ui_screen_t g_scr_home = {
    .name = "home", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_event = on_event, .on_tick = on_tick,
};
