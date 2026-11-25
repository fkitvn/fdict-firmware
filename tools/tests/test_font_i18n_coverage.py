"""Moi chuoi i18n (vi+en) ve duoc tren moi font nhan; font chu phai co chuoi fallback toi Noto (Viet+IPA).

Font Montserrat 24/32 chi co ASCII/icon — khong fallback thi 'Đúng %d/%d câu' bi o vuong (man Nghe, Chinh ta, ...).
Chay test nay tren cay chua gan .fallback se THAT BAI; sau khi gan fallback Noto 18 thi DAT.
"""
from pathlib import Path
import unittest

from font_util import (
    ROOT, c_phrases, font_c_paths, font_codepoints, font_fallback_name,
    instantiate, sample_cps,
)

# Font chi dung cho ky hieu / so, khong ve chu da dich. Ghi ro file:line noi gan font.
SYMBOL_NUMBER_FONTS = {
    "fd_font_batt_11": [
        "components/fd_ui/ui_widgets.c:174",  # nhan % pin, chi "0123456789%-"
    ],
}

# Font dung cho nhan chu (doi chieu test host render). LV_FONT_DEFAULT = noto_14.
LABEL_FONTS = ("fd_font_noto_12", "fd_font_noto_14", "fd_font_noto_18", "fd_font_mont_24", "fd_font_mont_32")
TITLE_FONT = "fd_font_noto_32_title"


def chain(name, seen=None):
    if seen is None:
        seen = []
    if name in seen:
        raise ValueError("fallback cycle: " + " -> ".join(seen + [name]))
    if name.startswith("lv_font_"):
        return seen + [name]
    fb = font_fallback_name(name)
    if not fb:
        return seen + [name]
    return chain(fb, seen + [name])


def chain_cps(name):
    cps = set()
    for n in chain(name):
        if n.startswith("lv_font_"):
            continue
        cps |= font_codepoints(n)
    return cps


class FontI18nCoverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data = ROOT / "tools/tests/data"
        cls.vi = sample_cps(data / "vi_samples.txt")
        cls.ipa = sample_cps(data / "ipa_samples.txt")
        cls.phrases = c_phrases()
        if not (len(cls.phrases) > 100 and len(cls.vi) > 100 and len(cls.ipa) > 40):
            raise RuntimeError("mau i18n/vi/ipa qua it")

    def test_mont_own_glyphs_miss_vietnamese(self):
        """Chung minh nguyen nhan: mont_32/24 tu than khong co glyph Viet (Đ, ú, ư, ụ)."""
        for name in ("fd_font_mont_32", "fd_font_mont_24"):
            cps = font_codepoints(name)
            missing = [cp for cp in (0x0110, 0x00FA, 0x1B0, 0x1EE5) if cp not in cps]
            self.assertEqual(missing, [0x0110, 0x00FA, 0x1B0, 0x1EE5], name + " bat ngo da co glyph Viet")

    def test_no_fallback_cycle_and_noto18_does_not_fall_to_mont(self):
        self.assertIsNone(font_fallback_name("fd_font_noto_18"))
        for p in font_c_paths():
            chain(p.stem)

    def test_mont_and_title_fallback_to_noto_18(self):
        for name in ("fd_font_mont_24", "fd_font_mont_32", TITLE_FONT):
            self.assertEqual(font_fallback_name(name), "fd_font_noto_18", name)
        sh = (ROOT / "tools/gen_fonts.sh").read_text(encoding="utf-8")
        self.assertIn("--lv-fallback fd_font_noto_18", sh)
        self.assertIn("fd_font_mont_", sh)

    def test_text_fonts_chain_covers_vietnamese_and_ipa(self):
        for p in font_c_paths():
            if p.stem in SYMBOL_NUMBER_FONTS:
                continue
            cps = chain_cps(p.stem)
            miss_vi = sorted(cp for cp in self.vi if cp not in cps)
            miss_ipa = sorted(cp for cp in self.ipa if cp not in cps)
            self.assertEqual(miss_vi[:12], [], p.stem + " thieu Viet (can fallback toi Noto)")
            self.assertEqual(miss_ipa[:12], [], p.stem + " thieu IPA (can fallback toi Noto)")

    def test_every_i18n_string_covered_by_every_label_font(self):
        fonts = list(LABEL_FONTS) + [TITLE_FONT]
        cover = {n: chain_cps(n) for n in fonts}
        bad = []
        for lang, key, raw in self.phrases:
            inst = instantiate(raw)
            for n, cps in cover.items():
                miss = sorted({ord(ch) for ch in inst if ord(ch) >= 0x20 and ord(ch) not in cps})
                if miss:
                    bad.append("%s %s %s U+%04X" % (n, lang, key, miss[0]))
                    if len(bad) >= 40:
                        break
            if len(bad) >= 40:
                break
        self.assertEqual(bad, [], "i18n thieu glyph (khong fallback?): " + "; ".join(bad))


if __name__ == "__main__":
    unittest.main(verbosity=2)
