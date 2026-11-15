/* Host render: font mac dinh (LV_FONT_DEFAULT) + moi font fd_* ve duoc chu Viet va LV_SYMBOL_* khong crash.
 * Bat loi 4.5.4: font boc .dsc = NULL -> LVGL 9 dat resolved_font = font boc -> NULL deref khi lay bitmap. */
#include <stdio.h>
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

int main(void)
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
    if (fails) return 1;
    printf("RENDER_OK\n");
    return 0;
}
