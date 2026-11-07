"""Release UI source/font-metric regressions. No device or live deck access."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
def text(p): return (ROOT / p).read_text(encoding="utf-8")

class Font:
    def __init__(self, name):
        self.name = name
        s = text("components/fd_fonts/" + name + ".c")
        def nums(name):
            a = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
            a = re.sub(r"/\*.*?\*/", "", a, flags=re.S)
            return [int(x, 0) for x in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", a)]
        self.adv = [int(x) for x in re.findall(r"\.adv_w = (\d+)", s)]
        self.left = nums("kern_left_class_mapping")
        self.right = nums("kern_right_class_mapping")
        self.kern = nums("kern_class_values")
        self.cols = int(re.search(r"\.right_class_cnt\s*=\s*(\d+)", s).group(1))
        self.scale = int(re.search(r"\.kern_scale\s*=\s*(\d+)", s).group(1))
        self.glyph = {}
        cmap = s.split("cmaps[]", 1)[1].split("/*-----------------", 1)[0]
        for start, length, gid, ul in re.findall(r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*\.unicode_list = (\w+)", cmap):
            offsets = range(int(length)) if ul == "NULL" else nums(ul)
            for i, v in enumerate(offsets): self.glyph[int(start) + v] = int(gid) + i
    def width(self, value):
        ids = [self.glyph[ord(c)] for c in value]
        width = 0
        for i, gid in enumerate(ids):
            kern = 0
            if i + 1 < len(ids) and self.left[gid] and self.right[ids[i + 1]]:
                kern = self.kern[(self.left[gid] - 1) * self.cols + self.right[ids[i + 1]] - 1]
            width += (self.adv[gid] + ((kern * self.scale) >> 4) + 8) >> 4
        return width

class ReleaseUiRegression(unittest.TestCase):
    def test_loading_copy_and_utf8(self):
        expected = "\u0110ang ki\u1ec3m tra phi\u00ean b\u1ea3n m\u1edbi..."
        for p in ["i18n/vi.yml", "components/fd_i18n/lv_i18n.c"]:
            self.assertIn(expected, text(p))
            self.assertFalse((ROOT / p).read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertIn("Checking for a new version...", text("i18n/en.yml"))
    def test_hide_entire_button_and_restore_from_state(self):
        s = text("components/fd_ui/scr_about.c")
        show = s.split("static void ota_show(void)")[1].split("/* ----------", 1)[0]
        self.assertIn("lv_obj_set_hidden(s_ota_btn, st.state == FD_OTA_ST_CHECKING)", show)
        self.assertLess(show.index("lv_obj_set_hidden(s_ota_btn"), show.index("if (!busy"))
        case = show.split("case FD_OTA_ST_CHECKING:", 1)[1].split("break;", 1)[0]
        self.assertIn('_("CHECKING")', case)
        self.assertNotIn("OTA_CONNECTING", case)
        self.assertIn("s_ota_btn = ob;", s)
        self.assertIn("s_ota_btn = s_ota_btn_lbl = NULL", s)
        self.assertEqual(s.count("fd_ota_check_now();"), 3)
        for tail in s.split("fd_ota_check_now();")[1:]: self.assertIn("ota_show();", tail.split("}", 1)[0])
    def test_check_claims_queue_synchronously_and_handles_failure(self):
        s = text("components/fd_ota/fd_ota.c").split("void fd_ota_check_now(void)")[1].split("void fd_ota_update_now", 1)[0]
        self.assertLess(s.index("s_st.state = FD_OTA_ST_CHECKING"), s.index("xQueueSend"))
        for v in ["FD_OTA_ST_CHECKING", "FD_OTA_ST_DOWNLOADING", "FD_OTA_ST_REBOOT_PENDING"]:
            self.assertIn("s_st.state != " + v, s)
        self.assertIn("s_st.state = FD_OTA_ST_ERROR", s)
        self.assertIn("s_st.check_err = true", s)
        self.assertIn("xSemaphoreGive(s_mx)", s)
    def test_english_no_wrap_on_both_faces(self):
        s = text("components/fd_ui/scr_flashcard.c")
        helper = s.split("static void word_label_set(")[1].split("static void card_refresh", 1)[0]
        for font in ["mont_32", "mont_24", "noto_18", "noto_14"]: self.assertIn("&fd_font_" + font, helper)
        self.assertIn("LV_LABEL_LONG_CLIP : LV_LABEL_LONG_SCROLL", helper)
        self.assertNotIn("LV_LABEL_LONG_WRAP", helper)
        self.assertIn("lv_obj_get_content_width(lv_obj_get_parent(face_obj))", helper)
        self.assertIn("word_label_set(lv_obj_get_child(s_mfront ? s_back : s_front, 0), c->word)", s)
        self.assertIn("lv_obj_update_layout(s_body)", helper)
        self.assertIn("char word[FD_FC_WORD_LEN]", helper)
    def test_long_words_and_phrases_font_metrics(self):
        fonts = [Font("fd_font_" + name) for name in ["mont_32", "mont_24", "noto_18", "noto_14"]]
        samples = ["responsibility", "international", "internationalization", "counterrevolutionaries",
                   "pneumonoultramicroscopicsilicovolcanoconiosis", "W" * 47,
                   "international responsibility", "take responsibility for", "mother-in-law", ""]
        # Includes conservative portrait and landscape content widths after speaker padding.
        for width in [64, 80, 86, 100, 140, 166, 196, 240]:
            for word in samples:
                chosen = next((f for f in fonts if f.width(word) <= width), fonts[-1])
                mode = "clip" if chosen.width(word) <= width else "scroll"
                if mode == "clip": self.assertLessEqual(chosen.width(word), width)
                else: self.assertEqual(chosen, fonts[-1])
                self.assertNotIn("\n", word)  # all phrases remain one line, no mid-word wrapping
                if width in (86, 166) and word in ("responsibility", "international", "W" * 47):
                    print(f"content={width}px {word}: {chosen.name} {chosen.width(word)}px {mode}")
        for word in ["responsibility", "international"]:
            self.assertTrue(any(f.width(word) <= 166 for f in fonts))
        self.assertGreater(fonts[-1].width("W" * 47), 166)
    def test_release_version(self):
        self.assertIn('set(PROJECT_VER "FDict One - 3.4.7")', text("CMakeLists.txt"))
        self.assertIn("## [3.4.7] - 2026-11-08", text("CHANGELOG.md"))

if __name__ == "__main__": unittest.main(verbosity=2)
