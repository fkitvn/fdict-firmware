#pragma once
/*
 * Font chu mac dinh cua LVGL: fd_font_noto_14 (Noto Sans 14, co dau tieng Viet).
 * -include file nay truoc moi file (CMakeLists.txt) va qua CONFIG_LV_FONT_CUSTOM_INCLUDE
 * de LV_FONT_DEFAULT khong roi ve Montserrat 14 (khong co glyph Viet -> o vuong).
 * Bieu tuong LV_SYMBOL_* lay tu fd_font_noto_14.fallback = lv_font_montserrat_14.
 * KHONG dung font boc (wrapper) co .dsc = NULL: LVGL 9 dat resolved_font = font boc roi doc ->dsc
 * khi lay bitmap -> NULL deref (crash). tools/tests/test_font_render_host.py kiem tra.
 */
#ifndef LV_FONT_DEFAULT
#define LV_FONT_DEFAULT (&fd_font_noto_14)
#endif
#ifndef LV_FONT_CUSTOM_DECLARE
#define LV_FONT_CUSTOM_DECLARE extern const lv_font_t fd_font_noto_14;
#endif
