#!/usr/bin/env bash
# Sinh font LVGL tieng Viet + IPA tu Noto Sans (OFL). Can Node.js (npx).
# Chay tu thu muc goc repo: bash tools/gen_fonts.sh
#
# Dai ky tu:
#   ASCII, Latin-1 (ç æ ð), Latin Extended-A day du (ŋ U+014B, ħ U+0127, œ, Đ Ĩ Ũ Ă)
#   Ơ Ư, chu Viet dung san U+1EA0-1EF9
#   IPA Extensions U+0250-02AF, Spacing Modifier Letters U+02B0-02FF (ʰ ʲ ʷ ˈ ˌ ː ˑ ˞ ˡ)
#   Combining Diacritics U+0300-036F (LVGL khong shaping: dau ve de len ky tu truoc theo offset cua font)
#   Hy Lap β U+03B2, θ U+03B8, χ U+03C7 (IPA dung chu Hy Lap, khong nam trong khoi IPA)
#   Phonetic Extensions + Supplement U+1D00-1DBF (ᵻ ᵿ ᵊ ...)
#   Dau cau: – — ‖ ‘ ’ “ ” • … ‿ ⁓ ⁿ ◌
set -euo pipefail
RANGES="0x20-0x7E,0xA0-0xFF,0x100-0x17F,0x1A0-0x1A1,0x1AF-0x1B0,0x1EA0-0x1EF9,\
0x250-0x2FF,0x300-0x36F,0x3B2,0x3B8,0x3C7,0x1D00-0x1DBF,\
0x2013-0x2014,0x2016,0x2018-0x201D,0x2022,0x2026,0x203F,0x2053,0x207F,0x25CC"
# 20/10 tung bo Spacing Modifier, Combining (tru dau Viet), Phonetic Ext -> phien am that bi o vuong (t͡ʃ d͡ʒ n̩ ʰ ᵻ).
# 15/11 (4.5.4): them lai du 3 khoi + β ⁓. Mau IPA that: tools/tests/data/ipa_samples.txt, test: tools/tests/test_ipa_glyphs.py.
# Noto 14 = font mac dinh (LV_FONT_DEFAULT): fallback Montserrat 14 cho LV_SYMBOL_*.
for SZ in 14 18; do
  FB=""; if [ "$SZ" = 14 ]; then FB="--lv-fallback lv_font_montserrat_14"; fi
  npx -y lv_font_conv@1.5.3 --font assets/fonts/NotoSans-Regular.ttf -r "$RANGES" \
    --size $SZ --bpp 4 --format lvgl --lv-include lvgl.h --lv-font-name fd_font_noto_${SZ} $FB \
    -o components/fd_fonts/fd_font_noto_${SZ}.c
done
# Tieu de 32 px (ui_screen_title): ASCII day du (tieu de tieng Anh, 1.3.0) + ky tu co dau cua tieu de tieng Viet.
npx -y lv_font_conv@1.5.3 --font assets/fonts/NotoSans-Regular.ttf -r 0x20-0x7E --symbols "àđặãĐáìĐềữáâạừậắếộ" \
  --size 32 --bpp 4 --format lvgl --lv-include lvgl.h --lv-font-name fd_font_noto_32_title --no-compress \
  -o components/fd_fonts/fd_font_noto_32_title.c
