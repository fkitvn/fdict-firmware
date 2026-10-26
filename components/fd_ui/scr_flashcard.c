/* Flash card (mockup, khong header, nen sang). 23/10: vao man -> CAI DAT truoc phien (cung kieu Hoc tap/scr_quiz):
 *   SETUP: So the (10/20/Tat ca), Thu tu (Theo danh sach/Ngau nhien), Mat truoc (Tu/Nghia), Tu dong phat (Tat/1/2 lan);
 *          cac lua chon trong vung cuon, nut [Bat dau] 46 px ghim day man. Nho trong NVS fd/fc_cfg.
 *   CARD : the hai mat (cham de lat, < Truoc / Sau >, bo dem); thu tu ngau nhien them nut Tron.
 * Quay lai (vuot mep / giu B1): CARD -> SETUP -> Home (on_back). (Bo man danh sach: Ngau nhien da nam trong cai dat.)
 * Du lieu (25/10, FDict One - 1.1.6): the tu server (fd_flashcard: POST /api/study/cards, Bearer token, cache /data) - cung mau
 *   Hoc tap (fd_study). Vao man -> tai nen; bam Bat dau -> chup danh sach the hien co. Chua ghep/offline/chua co cache ->
 *   10 tu mau co dinh ben duoi (ITEMS). */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "fd_fonts.h"
#include "fd_audio.h"
#include "fd_flashcard.h"
#include "fd_dict_mini.h"   /* struct fd_fc_item (word, ipa, meaning, audio TODO) */
#include "nvs.h"
#include "ui_priv.h"


static const struct fd_fc_item ITEMS[] = {
    {"apple", "/ˈæp.əl/", "quả táo", NULL},
    {"beautiful", "/ˈbjuː.tɪ.fəl/", "đẹp", NULL},
    {"journey", "/ˈdʒɜː.ni/", "chuyến đi, hành trình", NULL},
    {"knowledge", "/ˈnɒl.ɪdʒ/", "kiến thức", NULL},
    {"thought", "/θɔːt/", "suy nghĩ, ý nghĩ", NULL},
    {"weather", "/ˈweð.ər/", "thời tiết", NULL},
    {"measure", "/ˈmeʒ.ər/", "đo lường", NULL},
    {"curious", "/ˈkjʊə.ri.əs/", "tò mò", NULL},
    {"improve", "/ɪmˈpruːv/", "cải thiện", NULL},
    {"language", "/ˈlæŋ.ɡwɪdʒ/", "ngôn ngữ", NULL},
};
#define N_ITEMS ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

/* bo the dang dung: server/cache (s_net) hoac ITEMS */
static fd_fc_card_t *s_buf;                 /* FD_FC_MAX, PSRAM (chup tu fd_flashcard) */
static struct fd_fc_item s_net[FD_FC_MAX];
static const struct fd_fc_item *s_items = ITEMS;
static int s_nitems = N_ITEMS;

static void pick_items(void)
{
    s_items = ITEMS;
    s_nitems = N_ITEMS;
    if (!s_buf) s_buf = heap_caps_calloc(FD_FC_MAX, sizeof(fd_fc_card_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int n = s_buf ? fd_fc_copy(s_buf, FD_FC_MAX) : 0;
    if (n <= 0) return;
    for (int i = 0; i < n; i++) s_net[i] = (struct fd_fc_item){s_buf[i].word, s_buf[i].ipa, s_buf[i].meaning, NULL};
    s_items = s_net;
    s_nitems = n;
}

typedef enum { V_SETUP, V_CARD } view_t;
static view_t s_view;
static lv_obj_t *s_body;
static lv_obj_t *s_front, *s_back, *s_count;
static uint8_t s_order[FD_FC_MAX];
static int s_n;                  /* so the trong phien (<= s_nitems) */
static int s_pos;
static bool s_flipped;
static int32_t s_pad_hor0;       /* pad ngang mac dinh cua trang (SETUP giong Hoc tap) */

/* ---------- cau hinh (NVS fd/fc_cfg: byte0 so the, byte1 thu tu, byte2 mat truoc, byte3 tu dong phat | ver bit 28..31) ---------- */
#define FCFG_VER 1u
static uint8_t s_ncfg, s_rnd, s_mfront, s_reps;   /* 0/1/2 = 10/20/Tat ca; 0 danh sach/1 ngau nhien; 0 Tu/1 Nghia; 0 Tat/1/2 lan */
static const int NOPT[3] = {10, 20, 0};           /* 0 = Tat ca */

static void cfg_load(void)
{
    nvs_handle_t h;
    uint32_t v;
    if (nvs_open("fd", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u32(h, "fc_cfg", &v) == ESP_OK && (v >> 28) == FCFG_VER) {
        s_ncfg = (v & 0xFF) < 3 ? (uint8_t)(v & 0xFF) : 0;
        s_rnd = ((v >> 8) & 0xFF) ? 1 : 0;
        s_mfront = ((v >> 16) & 0xFF) ? 1 : 0;
        uint32_t r = (v >> 24) & 0x0F;
        s_reps = r <= 2 ? (uint8_t)r : 0;
    }
    nvs_close(h);
}

static void cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open("fd", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "fc_cfg", (uint32_t)s_ncfg | ((uint32_t)s_rnd << 8) | ((uint32_t)s_mfront << 16) |
                ((uint32_t)s_reps << 24) | (FCFG_VER << 28));
    nvs_commit(h);
    nvs_close(h);
}

/* ---------- CARD ---------- */
static void card_say(void)
{
    if (s_reps == 0) return;   /* Tat: chi nut loa */
    const fd_pron_cfg_t c = {FD_PRON_AUTO, s_reps, true};   /* nhu ui_pron_say (cache -> Pico + tai nen), lap 1/2 lan */
    ui_pron_say_cfg(s_items[s_order[s_pos]].word, &c);
}

static void card_refresh(void)
{
    const struct fd_fc_item *c = &s_items[s_order[s_pos]];
    /* Mat truoc = Tu: truoc [tu], sau [ipa, nghia]. Mat truoc = Nghia: truoc [nghia], sau [tu, ipa]. */
    lv_label_set_text(lv_obj_get_child(s_front, 0), s_mfront ? c->meaning : c->word);
    lv_label_set_text(lv_obj_get_child(s_back, 0), s_mfront ? c->word : c->ipa);
    lv_label_set_text(lv_obj_get_child(s_back, 1), s_mfront ? c->ipa : c->meaning);
    lv_obj_set_hidden(s_front, s_flipped);
    lv_obj_set_hidden(s_back, !s_flipped);
    lv_label_set_text_fmt(s_count, "%d/%d", s_pos + 1, s_n);
}

static void shuffle(void)
{
    for (int i = s_n - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        uint8_t t = s_order[i]; s_order[i] = s_order[j]; s_order[j] = t;
    }
    s_pos = 0;
    s_flipped = false;
}

static void flip_cb(lv_event_t *e) { (void)e; s_flipped = !s_flipped; card_refresh(); }
static void step_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_pos = (s_pos + d + s_n) % s_n;
    s_flipped = false;
    card_refresh();
    card_say();
}
static void shuffle_cb(lv_event_t *e) { (void)e; shuffle(); card_refresh(); card_say(); }
/* Phat am tu hien tai (task fd_audio, khong chan LVGL): co cache /data/voice/<tu>.pcm -> giong tot; khong -> Pico ngay +
 * xep tai nen (fd_voice, neu WiFi + da ghep) cho lan sau. Loi xep hang -> bip; loi tong hop ->
 * task am thanh phat chuong. Am luong 0 = im. Nut loa luon doc 1 lan. */
static void speak_cb(lv_event_t *e)
{
    (void)e;
    const struct fd_fc_item *c = &s_items[s_order[s_pos]];
    ui_pron_say(c->word);   /* duong chung: cache -> Pico + tai nen */
}

static lv_obj_t *face(lv_obj_t *card)
{
    lv_obj_t *f = lv_obj_create(card);
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(f, 6, 0);
    lv_obj_set_style_pad_hor(f, 50, 0);   /* chua cho nut loa 48px goc tren-phai */
    lv_obj_set_clickable(f, false);   /* cham roi vao the (cha) -> lat */
    return f;
}

static lv_obj_t *face_label(lv_obj_t *f, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(f);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static void nav_btn(lv_obj_t *row, const char *txt, lv_event_cb_t cb, intptr_t ud)
{
    lv_obj_t *b = lv_button_create(row);
    lv_obj_set_height(b, 42);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_pad_hor(b, 4, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);   /* chi chu Noto, khong LV_SYMBOL (font Noto khong co glyph symbol) */
    lv_obj_set_width(l, lv_pct(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);   /* doc: 3-4 nut/hang ~55 px */
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)ud);
}

/* Bo the theo cai dat: so the (gioi han theo so tu co), thu tu, mat truoc */
static void show_card(void)
{
    s_view = V_CARD;
    pick_items();
    s_n = NOPT[s_ncfg] && NOPT[s_ncfg] < s_nitems ? NOPT[s_ncfg] : s_nitems;
    for (int i = 0; i < s_nitems; i++) s_order[i] = (uint8_t)i;
    if (s_rnd) {   /* ngau nhien: tron toan bo roi lay s_n the dau */
        int n = s_n; s_n = s_nitems; shuffle(); s_n = n;
    }
    s_pos = 0;
    s_flipped = false;
    lv_obj_clean(s_body);
    lv_obj_set_scrollable(s_body, false);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_hor(s_body, 10, 0);
    lv_obj_set_style_pad_row(s_body, 6, 0);

    lv_obj_t *card = lv_obj_create(s_body);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_add_event_cb(card, flip_cb, LV_EVENT_CLICKED, NULL);

    /* Nut loa goc tren-phai (khong lat the: su kien CLICKED cua nut khong noi bot len the) */
    lv_obj_t *spk = lv_button_create(card);
    lv_obj_set_size(spk, 48, 48);   /* nut loa lon: tron amber, icon toi */
    lv_obj_align(spk, LV_ALIGN_TOP_RIGHT, 6, -6);
    lv_obj_set_style_radius(spk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(spk, 0, 0);
    lv_obj_set_style_bg_opa(spk, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_color(spk, lv_color_hex(FD_COLOR_PRIMARY_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(spk, 0, 0);
    lv_obj_add_event_cb(spk, speak_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *si = lv_label_create(spk);
    lv_obj_set_style_text_font(si, &fd_font_mont_24, 0);
    lv_obj_set_style_text_color(si, lv_color_hex(FD_COLOR_ON_PRIMARY), 0);
    lv_label_set_text(si, LV_SYMBOL_VOLUME_MAX);
    lv_obj_center(si);

    /* tu tieng Anh: Montserrat 32 (ASCII); nghia tieng Viet: Noto 18; IPA: Noto 18 mau nhan */
    s_front = face(card);
    face_label(s_front, s_mfront ? &fd_font_noto_18 : &fd_font_mont_32, 0x111827);
    lv_obj_t *h = lv_label_create(s_front);
    lv_obj_set_style_text_color(h, lv_color_hex(0x9CA3AF), 0);
    lv_label_set_text(h, _("TAP_TO_FLIP"));

    s_back = face(card);
    if (s_mfront) {
        face_label(s_back, &fd_font_mont_32, 0x111827);
        face_label(s_back, &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
    } else {
        face_label(s_back, &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);
        face_label(s_back, &fd_font_noto_18, 0x111827);
    }
    lv_obj_move_foreground(spk);   /* tren cac mat the */

    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    nav_btn(row, _("PREV"), step_cb, -1);
    s_count = lv_label_create(row);
    lv_obj_set_style_text_color(s_count, lv_color_hex(0x6B7280), 0);
    if (s_rnd) nav_btn(row, _("SHUFFLE"), shuffle_cb, 0);
    nav_btn(row, _("NEXT"), step_cb, 1);
    card_refresh();
    card_say();
}

/* ---------- SETUP (cung kieu man cai dat Hoc tap, scr_quiz.c) ---------- */
enum { OPT_N, OPT_ORD, OPT_FRONT, OPT_REP };
static void show_setup(void);

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = lv_label_create(p);
    if (f) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, t);
    return l;
}

static lv_obj_t *hrow(lv_obj_t *p, int32_t h)
{
    lv_obj_t *r = lv_obj_create(p);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    return r;
}

static lv_obj_t *btn(lv_obj_t *p, const char *t, bool primary, lv_event_cb_t cb, intptr_t ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_height(b, 36);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_hex(FD_COLOR_PRIMARY) : lv_color_white(), 0);
    lv_obj_set_style_border_width(b, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(FD_COLOR_BORDER), 0);
    lv_obj_set_style_pad_hor(b, 6, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)ud);
    return b;
}

/* Su kien nut -> lv_async_call: dung lai man (xoa nut dang xu ly) ngoai callback, khong chay sau trong ngan xep su kien */
static void setup_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_setup(); }
static void start_async(void *arg) { (void)arg; if (s_body && s_view == V_SETUP) show_card(); }

static void opt_cb(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int grp = (int)(ud >> 4), v = (int)(ud & 0xF);
    if (grp == OPT_N) s_ncfg = (uint8_t)v;
    else if (grp == OPT_ORD) s_rnd = (uint8_t)v;
    else if (grp == OPT_FRONT) s_mfront = (uint8_t)v;
    else if (grp == OPT_REP) s_reps = (uint8_t)v;
    lv_async_call(setup_async, NULL);
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    cfg_save();
    lv_async_call(start_async, NULL);
}

static void opt_row(lv_obj_t *p, const char *name, int grp, const char *const *opts, int n, int sel)
{
    /* hang cao theo noi dung (khong cat nut): ngang 34 px, doc 36 px */
    bool por = lv_display_get_horizontal_resolution(NULL) < lv_display_get_vertical_resolution(NULL);
    int32_t bh = por ? 36 : 34;
    lv_obj_t *r = hrow(p, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(r, false);
    lv_obj_t *l = label(r, name, NULL, FD_COLOR_TEXT_MUTED);
    if (grp == OPT_REP || grp == OPT_FRONT) {   /* nhan dai: rong hon + xuong 2 dong khi doc */
        lv_obj_set_width(l, por ? 56 : 92);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(l, -3, 0);
    } else lv_obj_set_width(l, por ? 48 : 62);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = btn(r, _(opts[i]), i == sel, opt_cb, (grp << 4) | i);
        lv_obj_set_height(b, bh);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_pad_hor(b, 2, 0);
        lv_obj_t *bl = lv_obj_get_child(b, 0);
        lv_obj_set_width(bl, lv_pct(100)); lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
        if (grp == OPT_ORD && por) {   /* doc: "Theo danh sach" xuong 2 dong trong nut 36 px */
            lv_label_set_long_mode(bl, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_line_space(bl, -3, 0);
        } else lv_label_set_long_mode(bl, LV_LABEL_LONG_DOT);
    }
}

static void show_setup(void)
{
    s_view = V_SETUP;
    s_front = s_back = s_count = NULL;
    lv_obj_clean(s_body);
    lv_obj_set_scrollable(s_body, false);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_hor(s_body, s_pad_hor0, 0);
    lv_obj_set_style_pad_row(s_body, 4, 0);
    /* cai dat trong vung cuon (flex_grow) phia tren; nut Bat dau ghim day man */
    lv_obj_t *sc = lv_obj_create(s_body);
    lv_obj_remove_style_all(sc);
    lv_obj_set_width(sc, lv_pct(100));
    lv_obj_set_flex_grow(sc, 1);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sc, 6, 0);
    lv_obj_set_style_pad_bottom(sc, 4, 0);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *tr = hrow(sc, 26);
    lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(tr, "Flash card", &fd_font_noto_18, FD_COLOR_PRIMARY_TEXT);

    static const char *const NS[] = {"10", "20", "ALL"};
    static const char *const ORD[] = {"IN_ORDER", "RANDOM"};
    static const char *const FR[] = {"WORD", "MEANING"};
    static const char *const RP[] = {"OFF", "REPEAT_1", "REPEAT_2"};
    opt_row(sc, _("CARDS"), OPT_N, NS, 3, s_ncfg);
    opt_row(sc, _("ORDER"), OPT_ORD, ORD, 2, s_rnd);
    opt_row(sc, _("FRONT_SIDE"), OPT_FRONT, FR, 2, s_mfront);
    opt_row(sc, _("AUTO_PLAY"), OPT_REP, RP, 3, s_reps);

    lv_obj_t *b = btn(s_body, _("START"), true, start_cb, 0);
    lv_obj_set_size(b, lv_pct(100), 46);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 6, 0);
    lv_obj_set_style_shadow_offset_y(b, 2, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(FD_COLOR_PRIMARY), 0);
    lv_obj_t *bl = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_font(bl, &fd_font_noto_18, 0);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(bl, 1, 0);
}

static void build(lv_obj_t *scr)
{
    s_body = ui_page_create(scr);
    s_pad_hor0 = lv_obj_get_style_pad_left(s_body, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_body, 6, 0);
    cfg_load();
    fd_fc_refresh();   /* tai the nen (da ghep + WiFi); khong chan */
    show_setup();
}

static void destroy(void) { s_body = s_front = s_back = s_count = NULL; }

/* Quay lai: tu the ve cai dat truoc (async: khong xoa obj dang xu ly su kien) */
static void back_setup_async(void *arg) { (void)arg; if (s_body) show_setup(); }
static bool on_back(void)
{
    if (s_view == V_SETUP) return false;
    lv_async_call(back_setup_async, NULL);
    return true;
}

const ui_screen_t g_scr_flashcard = {
    .name = "flashcard", .parent = FD_SCR_HOME, .build = build, .destroy = destroy, .on_back = on_back,
};
