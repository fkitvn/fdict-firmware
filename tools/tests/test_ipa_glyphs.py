"""Phien am (IPA) va chu Viet phai co glyph trong font cua moi nhan phien am (khong o vuong).
Mau IPA that: tools/tests/data/ipa_samples.txt (Wiktionary qua dictionaryapi.dev, cung nguon web-app).
Ve that tren host: tools/tests/test_font_render_host.py + host_render/main.c."""
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_font_default import font_codepoints, text  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "tools" / "tests" / "data"
UI = ROOT / "components" / "fd_ui"
NOTO = {"fd_font_noto_14", "fd_font_noto_18"}
# Man co hien phien am. Them man moi co ipa -> them vao day va kiem font cua nhan.
IPA_SCREENS = {"scr_dict.c", "scr_flashcard.c", "scr_practice.c", "scr_spell.c", "scr_sync_mcq.c"}


def sample_chars(name):
    out = set()
    for line in (DATA / name).read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        out |= {ch for ch in line.split("\t", 1)[-1] if ord(ch) >= 0x20}
    return out


def missing(chars, font):
    have = font_codepoints(font)
    return " ".join("U+%04X %s" % (ord(c), c) for c in sorted(chars, key=ord) if ord(c) not in have)


class IpaGlyphs(unittest.TestCase):
    def test_ipa_samples_in_noto_14_and_18(self):
        need = sample_chars("ipa_samples.txt")
        self.assertGreater(len(need), 40)
        for f in sorted(NOTO):
            self.assertEqual(missing(need, f), "", f + " thieu glyph phien am (o vuong)")

    def test_vietnamese_samples_in_noto_14_and_18(self):
        need = sample_chars("vi_samples.txt")
        self.assertGreater(len(need), 100)
        for f in sorted(NOTO):
            self.assertEqual(missing(need, f), "", f + " thieu glyph chu Viet")

    def test_gen_fonts_ranges_match_generated_fonts(self):
        sh = text("tools/gen_fonts.sh").replace("\r", "")
        m = re.search(r'^RANGES="(.*?)"', sh, re.S | re.M)
        self.assertTrue(m, "khong thay RANGES trong gen_fonts.sh")
        ranges = re.sub(r"\\\n", "", m.group(1))
        for f in sorted(NOTO):
            opts = re.search(r"Opts:.*? -r (\S+)", text("components/fd_fonts/%s.c" % f))
            self.assertTrue(opts, f)
            self.assertEqual(opts.group(1), ranges, f + " khong sinh tu RANGES hien tai cua gen_fonts.sh")

    def test_every_phonetic_label_uses_noto(self):
        found = set()
        for p in sorted(UI.glob("*.c")):
            s = p.read_text(encoding="utf-8")
            if not re.search(r"(->|\.)ipa\b", s):
                continue
            found.add(p.name)
            # label(parent, x->ipa, FONT, color): FONT NULL (ke thua theme Noto 14) hoac Noto 14/18.
            for font in re.findall(r"\blabel\([^;]*?(?:->|\.)ipa\s*,\s*([^,]+),", s):
                font = font.strip().lstrip("&")
                self.assertIn(font, NOTO | {"NULL"}, "%s: nhan phien am dung font %s" % (p.name, font))
            if re.search(r"\blabel\([^;]*?(?:->|\.)ipa\s*,\s*NULL", s):
                helper = re.search(r"static lv_obj_t \*label\([^)]*\)\s*\{(.*?)\n\}", s, re.S)
                self.assertTrue(helper and "if (f) lv_obj_set_style_text_font" in helper.group(1),
                                p.name + ": label(..., NULL) phai ke thua font man hinh")
        self.assertEqual(found, IPA_SCREENS, "man hien phien am thay doi: cap nhat IPA_SCREENS va kiem font")
        # Font man hinh (ke thua cho label NULL) = Noto 14.
        self.assertRegex(text("components/fd_ui/ui_nav.c"), r"lv_theme_default_init\([^;]*&fd_font_noto_14\)")
        # Lat tu: IPA o mat sau, con 1 khi mat truoc = Nghia, con 0 khi mat truoc = Tu.
        fc = text("components/fd_ui/scr_flashcard.c")
        self.assertIn("lv_label_set_text(lv_obj_get_child(s_back, 0), c->ipa)", fc)
        self.assertIn("lv_label_set_text(lv_obj_get_child(s_back, 1), s_mfront ? c->ipa : c->meaning)", fc)
        blk = re.search(r"s_back = face\(card\);\s*if \(s_mfront\) \{(.*?)\} else \{(.*?)\}", fc, re.S)
        self.assertTrue(blk, "khong thay khoi tao mat sau Lat tu")
        mf = re.findall(r"face_label\(s_back, &(\w+)", blk.group(1))
        wf = re.findall(r"face_label\(s_back, &(\w+)", blk.group(2))
        self.assertIn(mf[1], NOTO, "Lat tu (mat truoc Nghia): nhan IPA khong dung Noto")
        self.assertIn(wf[0], NOTO, "Lat tu (mat truoc Tu): nhan IPA khong dung Noto")


if __name__ == "__main__":
    unittest.main(verbosity=2)
