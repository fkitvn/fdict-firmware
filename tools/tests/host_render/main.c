/* Host render: font mac dinh (LV_FONT_DEFAULT) + moi font fd_* ve duoc chu Viet va LV_SYMBOL_* khong crash.
 * Bat loi 4.5.4: font boc .dsc = NULL -> LVGL 9 dat resolved_font = font boc -> NULL deref khi lay bitmap.
 * Phien am: moi ky tu trong tools/tests/data/ipa_samples.txt (IPA that) va vi_samples.txt phai co glyph that
 * (khong phai o vuong placeholder) trong font cua tung nhan phien am. Doi so: <ipa_samples> <vi_samples>. */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "fd_fonts.h"

static uint8_t s_buf[320 * 40 * 2];
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *p) { (void)a; (void)p; lv_display_flush_ready(d); }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* Moi font trong chuoi fallback phai la font that (dsc != NULL), khong phai font boc. */
static void check_chain(const char *name, const lv_font_t *f)
{
    int depth = 0;
    for (const lv_font_t *x = f; x && depth < 8; x = x->fallback, depth++)
        CHECK(x->dsc != NULL, "%s: font o muc fallback %d co dsc NULL (font boc)", name, depth);
}

static void check_glyph(const char *name, const lv_font_t *f, uint32_t cp, const lv_font_t *want)
{
    lv_font_glyph_dsc_t g;
    bool ok = lv_font_get_glyph_dsc(f, &g, cp, 0);
    CHECK(ok && !g.is_placeholder, "%s: U+%04X khong co glyph", name, (unsigned)cp);
    if (!ok) return;
    CHECK(g.resolved_font && g.resolved_font->dsc, "%s: U+%04X resolved_font khong co dsc", name, (unsigned)cp);
    if (want) CHECK(g.resolved_font == want, "%s: U+%04X resolved sai font", name, (unsigned)cp);
}

/* Giai ma 1 ky tu UTF-8 (khong dung ham private cua LVGL). */
static uint32_t utf8_next(const char *t, uint32_t *i)
{
    const unsigned char *p = (const unsigned char *)t + *i;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; n = 2; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; n = 3; }
    else { cp = p[0] & 0x07; n = 4; }
    for (int k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) { *i += k; return 0xFFFD; }
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    *i += n;
    return cp;
}

/* Doc tep mau UTF-8: bo dong '#', dong "tu<TAB>chuoi" chi lay phan sau TAB. Tra so ky tu khac nhau.
 * sample nhan dong dau (last = false) hoac dong cuoi (last = true) de ve that. */
#define MAX_CP 1024
static int load_cps(const char *path, uint32_t *out, int max, char *sample, size_t sample_n, bool last)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL: khong mo duoc %s\n", path); fails++; return 0; }
    char line[2048];
    int n = 0;
    sample[0] = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char *t = strchr(line, '\t');
        t = t ? t + 1 : line;
        t[strcspn(t, "\r\n")] = 0;
        if (last || !sample[0]) snprintf(sample, sample_n, "%s", t);
        for (uint32_t i = 0; t[i];) {
            uint32_t cp = utf8_next(t, &i);
            if (cp < 0x20) continue;
            int k = 0;
            while (k < n && out[k] != cp) k++;
            if (k == n && n < max) out[n++] = cp;
        }
    }
    fclose(f);
    return n;
}

/* Moi ky tu phai ra glyph that cua dung font Noto (khong placeholder, khong roi xuong Montserrat). */
static void check_text(const char *site, const lv_font_t *f, const uint32_t *cps, int n)
{
    int bad = 0;
    for (int i = 0; i < n; i++) {
        lv_font_glyph_dsc_t g;
        bool ok = lv_font_get_glyph_dsc(f, &g, cps[i], 0);
        if (!ok || g.is_placeholder || g.resolved_font != f) {
            if (bad < 12) printf("FAIL: %s: U+%04X khong co glyph Noto (o vuong)\n", site, (unsigned)cps[i]);
            bad++;
        }
    }
    if (bad) { printf("FAIL: %s: %d ky tu thieu glyph\n", site, bad); fails++; }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* in loi truoc khi crash */
    lv_init();
    lv_display_t *d = lv_display_create(320, 240);
    lv_display_set_buffers(d, s_buf, NULL, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    const lv_font_t *def = LV_FONT_DEFAULT;
    CHECK(def == &fd_font_noto_14, "LV_FONT_DEFAULT khong phai fd_font_noto_14");
    check_glyph("default", def, 0x1EE7, &fd_font_noto_14);          /* u hoi */
    check_glyph("default", def, 0x00E1, &fd_font_noto_14);          /* a sac */
    check_glyph("default", def, 0xF00C, &lv_font_montserrat_14);    /* LV_SYMBOL_OK */
    check_glyph("default", def, 0xF053, &lv_font_montserrat_14);    /* LV_SYMBOL_LEFT */

    static const struct { const char *n; const lv_font_t *f; } F[] = {
        {"default", LV_FONT_DEFAULT}, {"noto_14", &fd_font_noto_14}, {"noto_18", &fd_font_noto_18},
        {"noto_32_title", &fd_font_noto_32_title}, {"mont_24", &fd_font_mont_24}, {"mont_32", &fd_font_mont_32},
        {"montserrat_14", &lv_font_montserrat_14},
    };
    for (unsigned i = 0; i < sizeof(F) / sizeof(F[0]); i++) check_chain(F[i].n, F[i].f);

    /* Ve that: nhan khong gan font (theme + layer), va nhan gan tung font. */
    const char *txt = "H\xe1\xbb\xa7y Tho\xc3\xa1t \xe1\xbb\xa7 \xc3\xa1 " LV_SYMBOL_OK " " LV_SYMBOL_LEFT;
    lv_label_set_text(lv_label_create(lv_screen_active()), txt);
    lv_label_set_text(lv_label_create(lv_layer_top()), txt);
    lv_label_set_text(lv_label_create(lv_layer_sys()), txt);
    for (unsigned i = 0; i < sizeof(F) / sizeof(F[0]); i++) {
        lv_obj_t *l = lv_label_create(lv_screen_active());
        lv_obj_set_style_text_font(l, F[i].f, 0);
        lv_obj_set_y(l, 30 + (int32_t)i * 28);
        lv_label_set_text(l, txt);
    }
    lv_refr_now(d);

    /* Phien am: font that cua tung nhan phien am.
     * Tu dien, Luyen tap, Phan loai tu: label(..., ipa, NULL, ...) -> ke thua font man hinh (theme Noto 14, ui_nav.c).
     * Chinh ta: Noto 14. Lat tu: Noto 18 (scr_flashcard.c face_label). */
    if (argc < 3) { printf("FAIL: thieu doi so <ipa_samples> <vi_samples>\n"); return 1; }
    static uint32_t ipa[MAX_CP], vi[MAX_CP];
    static char ipa_line[1024], vi_line[1024];
    int n_ipa = load_cps(argv[1], ipa, MAX_CP, ipa_line, sizeof(ipa_line), true);   /* bang ky hieu IPA */
    int n_vi = load_cps(argv[2], vi, MAX_CP, vi_line, sizeof(vi_line), false);   /* bang chu Viet */
    CHECK(n_ipa > 40 && n_vi > 100, "tep mau qua it ky tu (ipa %d, vi %d)", n_ipa, n_vi);
    lv_theme_t *th = lv_theme_default_init(d, lv_color_hex(0x2563EB), lv_color_hex(0x1E40AF), false, &fd_font_noto_14);
    lv_display_set_theme(d, th);
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load(scr);
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);   /* giong s_body / pr: khung khong style */
    lv_obj_t *inh = lv_label_create(body);
    const lv_font_t *inh_font = lv_obj_get_style_text_font(inh, LV_PART_MAIN);
    CHECK(inh_font == &fd_font_noto_14, "nhan phien am khong gan font khong ke thua Noto 14");
    static const struct { const char *n; const lv_font_t *f; } P[] = {
        {"phien am ke thua (Tu dien/Luyen tap/Phan loai tu)", NULL},
        {"phien am Chinh ta (noto_14)", &fd_font_noto_14},
        {"phien am Lat tu (noto_18)", &fd_font_noto_18},
    };
    printf("IPA %d ky tu, Viet %d ky tu\n", n_ipa, n_vi);
    for (unsigned i = 0; i < sizeof(P) / sizeof(P[0]); i++) {
        const lv_font_t *f = P[i].f ? P[i].f : inh_font;
        char site[96];
        snprintf(site, sizeof(site), "%s IPA", P[i].n);
        check_text(site, f, ipa, n_ipa);
        snprintf(site, sizeof(site), "%s Viet", P[i].n);
        check_text(site, f, vi, n_vi);
        lv_obj_t *l = P[i].f ? lv_label_create(body) : inh;
        if (P[i].f) lv_obj_set_style_text_font(l, P[i].f, 0);
        lv_obj_set_width(l, 300);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_label_set_text(l, ipa_line);
        lv_label_set_text(lv_label_create(body), vi_line);
    }
    lv_refr_now(d);
    if (fails) return 1;
    printf("RENDER_OK\n");
    return 0;
}
