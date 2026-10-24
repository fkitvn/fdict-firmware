#pragma once
/*
 * Font Noto Sans (SIL OFL 1.1, assets/fonts/OFL.txt) sinh bang lv_font_conv 1.5.3 (tools/gen_fonts.sh).
 * Gom: ASCII, Latin-1, chu Viet day du (U+1EA0-1EF9, Ă Đ Ĩ Ũ Ơ Ư), IPA Extensions, ˈ ˌ ː, dau nhay/gach.
 * Khong co LV_SYMBOL_* -> dung lv_font_montserrat_14 cho icon.
 */
#include "lvgl.h"

LV_FONT_DECLARE(fd_font_noto_14);
LV_FONT_DECLARE(fd_font_noto_18);
LV_FONT_DECLARE(fd_font_noto_32_title);   /* 32 px, chi glyph 'Cai dat' co dau (tieu de Cai dat) */
/* 23/10: Montserrat Medium 24/32 rieng (lv_font_conv, nen RLE): ASCII + ° + • + chi LV_SYMBOL_* firmware dung (thay lv_font_montserrat_24/32 built-in, giam ~50 KB) */
LV_FONT_DECLARE(fd_font_mont_24);
LV_FONT_DECLARE(fd_font_mont_32);
