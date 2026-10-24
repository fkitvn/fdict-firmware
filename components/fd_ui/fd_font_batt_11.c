/*******************************************************************************
 * Size: 11 px
 * Bpp: 4
 * Opts: --font NotoSans-Regular.ttf --symbols 0123456789%- --size 11 --bpp 4 --format lvgl --lv-include lvgl.h --lv-font-name fd_font_batt_11 -o fd_font_batt_11.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FD_FONT_BATT_11
#define FD_FONT_BATT_11 1
#endif

#if FD_FONT_BATT_11

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0025 "%" */
    0xb, 0xc3, 0x0, 0x60, 0x1, 0x73, 0xa4, 0x1e,
    0x40, 0x4, 0x53, 0xd, 0xa8, 0x0, 0x8a, 0x68,
    0x9a, 0xa0, 0x2f, 0x4f, 0x16, 0xf6, 0x38, 0x4d,
    0xf4, 0x93, 0x2f, 0x0, 0xc, 0xc4, 0x41, 0x7e,
    0x0, 0x44, 0x2, 0xa7, 0xd8,

    /* U+002D "-" */
    0x7d, 0xd0, 0x80,

    /* U+0030 "0" */
    0x5, 0xdd, 0x40, 0xd, 0xe6, 0x9a, 0x24, 0x21,
    0xda, 0x44, 0x0, 0x9, 0x8, 0x7, 0x19, 0x0,
    0x4, 0x95, 0x4, 0x3b, 0x2, 0xf3, 0x49, 0x40,

    /* U+0031 "1" */
    0x0, 0x47, 0x5, 0x0, 0x57, 0x0, 0x1, 0x0,
    0xff, 0xe3, 0x0,

    /* U+0032 "2" */
    0x19, 0xdd, 0x48, 0x3, 0x37, 0x4a, 0x82, 0xa0,
    0x6, 0x10, 0x8, 0x5d, 0x0, 0x2d, 0x91, 0x0,
    0x5c, 0xa0, 0x2, 0xf9, 0x40, 0xe, 0x97, 0xdc,
    0xa0,

    /* U+0033 "3" */
    0x2a, 0xcd, 0xa0, 0x28, 0xcd, 0x15, 0x2, 0x1,
    0x76, 0x4, 0xde, 0xb3, 0x4, 0xde, 0xc6, 0x0,
    0x87, 0x70, 0x80, 0x2d, 0xd3, 0xee, 0xc4, 0xe0,

    /* U+0034 "4" */
    0x0, 0x9f, 0x80, 0x31, 0xe0, 0x7, 0x7d, 0x0,
    0x69, 0x90, 0x80, 0x4a, 0xa, 0x1, 0xb9, 0xfb,
    0x83, 0xc3, 0x7d, 0xd0, 0xf0, 0x80, 0x7c,

    /* U+0035 "5" */
    0xd, 0xee, 0x84, 0xf, 0xba, 0x10, 0x30, 0xc,
    0x25, 0x9b, 0x0, 0x3f, 0x9a, 0x6e, 0x1, 0xbb,
    0x82, 0x1, 0x7f, 0x8b, 0x37, 0x44, 0xc0,

    /* U+0036 "6" */
    0x0, 0x4e, 0xe1, 0x5, 0x76, 0xe1, 0xb, 0xb8,
    0x2, 0x56, 0xac, 0xa1, 0x13, 0x5e, 0x6c, 0xb,
    0x88, 0x21, 0x2b, 0x88, 0x59, 0x8c, 0xee, 0x8a,
    0x40,

    /* U+0037 "7" */
    0x7e, 0xed, 0x6f, 0xdd, 0x36, 0x80, 0x66, 0x60,
    0x4, 0xec, 0x1, 0xa6, 0x40, 0x12, 0xa0, 0x80,
    0x51, 0x20, 0x12, 0x9, 0x80, 0x0,

    /* U+0038 "8" */
    0x7, 0xcc, 0x50, 0x15, 0x66, 0xd, 0x8c, 0xc2,
    0x6, 0x43, 0x5f, 0x9a, 0x81, 0xf3, 0x74, 0x4a,
    0x4e, 0xe8, 0x80, 0x18, 0x0, 0xcc, 0xa7, 0x98,
    0xd8,

    /* U+0039 "9" */
    0x7, 0xdd, 0x38, 0x1d, 0xee, 0xb1, 0x15, 0x0,
    0x1d, 0xa4, 0x80, 0xf, 0x24, 0xbc, 0xb2, 0x20,
    0x3e, 0x58, 0x78, 0x4, 0x6a, 0xe1, 0x79, 0xfc,
    0x20
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 146, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 37, .adv_w = 57, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 40, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 101, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 75, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 100, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 124, .adv_w = 101, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 147, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 170, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 195, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 217, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x8
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 37, .range_length = 9, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 48, .range_length = 10, .glyph_id_start = 3,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t fd_font_batt_11 = {
#else
lv_font_t fd_font_batt_11 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 8,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FD_FONT_BATT_11*/

