"""i18n: vi.yml / en.yml va lv_i18n.c (sua tay, KHONG sinh lai) phai cung bo khoa; moi khoa dung trong code phai co."""
from pathlib import Path
import re
import unittest
from collections import Counter

ROOT = Path(__file__).resolve().parents[2]
C_FILE = ROOT / "components/fd_i18n/lv_i18n.c"
# Chuoi viet tat co y khong dich (_() tra lai nguyen van), vd nhan man Thong tin.
LITERAL_OK = {"IP", "MAC", "PSRAM"}


def yml_keys(lang):
    s = (ROOT / "i18n" / (lang + ".yml")).read_text(encoding="utf-8")
    return re.findall(r"^  '?([A-Z0-9_]+)'?\s*:", s, re.M)


def c_table(name):
    s = C_FILE.read_text(encoding="utf-8")
    m = re.search(r"static const lv_i18n_phrase_t " + name + r"\[\] = \{(.*?)\n\};", s, re.S)
    if not m:
        return None
    return re.findall(r'^\s*\{"([A-Z0-9_]+)", "((?:[^"\\]|\\.)*)"\}', m.group(1), re.M)


def fmt_specs(t):
    return re.findall(r"%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z%]", t)


def used_keys():
    keys = {}
    arr_re = re.compile(r"static const char \*const (\w+)\[[^\]]*\]\s*=\s*\{(.*?)\};", re.S)
    for p in sorted((ROOT / "components").rglob("*.c")):
        if p.name == "lv_i18n.c":
            continue
        s = p.read_text(encoding="utf-8", errors="ignore")
        for k in re.findall(r'\b_\("([A-Z][A-Z0-9_]*)"\)', s):
            keys.setdefault(k, p.name)
        # mang khoa truyen qua _(ARR[i]) hoac ui_setup_opt_row
        for name, body in arr_re.findall(s):
            # chi mang that su di qua _(): _(ARR[i]) hoac tuy chon ui_setup_opt_row(..., ARR, ...)
            if not re.search(r"\b_\(\s*" + name + r"\[", s) and \
                    not re.search(r"ui_setup_opt_row\([^;]*\b" + name + r"\b", s):
                continue
            for k in re.findall(r'"([A-Z][A-Z0-9_]+)"', body):
                keys.setdefault(k, p.name + " (mang " + name + ")")
    return keys


class I18nTables(unittest.TestCase):
    def setUp(self):
        self.en = c_table("en_singulars")
        self.vi = c_table("vi_singulars")
        self.assertIsNotNone(self.en)
        self.assertIsNotNone(self.vi)

    def test_no_duplicates(self):
        for name, rows in (("en_singulars", self.en), ("vi_singulars", self.vi)):
            d = [k for k, n in Counter(k for k, _ in rows).items() if n > 1]
            self.assertEqual(d, [], name + " trung khoa")
        for lang in ("en", "vi"):
            d = [k for k, n in Counter(yml_keys(lang)).items() if n > 1]
            self.assertEqual(d, [], lang + ".yml trung khoa")

    def test_yml_and_c_have_same_keys(self):
        ce, cv = {k for k, _ in self.en}, {k for k, _ in self.vi}
        ye, yv = set(yml_keys("en")), set(yml_keys("vi"))
        self.assertEqual(sorted(ce ^ cv), [], "en_singulars khac vi_singulars")
        self.assertEqual(sorted(ye ^ yv), [], "en.yml khac vi.yml")
        self.assertEqual(sorted(ye - ce), [], "co trong yml nhung thieu trong lv_i18n.c (sua tay lv_i18n.c)")
        self.assertEqual(sorted(ce - ye), [], "co trong lv_i18n.c nhung thieu trong yml")

    def test_format_args_match_between_languages(self):
        vi = dict(self.vi)
        bad = [k for k, t in self.en if k in vi and fmt_specs(t) != fmt_specs(vi[k])]
        self.assertEqual(bad, [], "dinh dang %d/%s khac nhau giua en va vi")

    def test_every_used_key_exists(self):
        have = {k for k, _ in self.vi}
        missing = sorted("%s (%s)" % (k, f) for k, f in used_keys().items() if k not in have and k not in LITERAL_OK)
        self.assertEqual(missing, [], "khoa dung trong code nhung thieu trong lv_i18n.c")


if __name__ == "__main__":
    unittest.main(verbosity=2)
