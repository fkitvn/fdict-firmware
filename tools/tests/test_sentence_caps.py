# -*- coding: utf-8 -*-
"""Gioi han cau tren may: 15 tu, bo nho chu du cau 200 ky tu, bo 9 tu van dung duoc."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def macro(src, name):
    m = re.search(r"#define\s+%s\s+(\d+)" % name, src)
    if not m:
        raise AssertionError("thieu " + name)
    return int(m.group(1))


class TestSentenceCaps(unittest.TestCase):
    def test_token_and_text_caps(self):
        sen = read("components/fd_ota/include/fd_sentence.h")
        study = read("components/fd_ota/include/fd_study.h")
        self.assertEqual(macro(sen, "FD_SEN_TOK"), 15)
        self.assertEqual(macro(sen, "FD_SEN_TOK_LEN"), 16)
        self.assertGreaterEqual(macro(sen, "FD_SEN_TXT_LEN"), 208)
        self.assertGreaterEqual(macro(study, "FD_STUDY_TXT_LEN"), 208)
        self.assertRegex(study, r"char\s+sen\[(FD_STUDY_TXT_LEN|FD_SEN_TXT_LEN)\]")

    def test_arrays_follow_macros(self):
        listen = read("components/fd_ui/scr_listen.c")
        sentence = read("components/fd_ui/scr_sentence.c")
        for src in (listen, sentence):
            self.assertIn("FD_SEN_TOK", src)
        self.assertNotRegex(listen, r"s_box\[9\]|s_buf\[10\]")
        self.assertNotRegex(sentence, r"s_tile\[9\]|s_used\[9\]")

    def test_make_ex_keeps_old_nine_word_valid(self):
        sync = read("components/fd_ota/fd_sync.c")
        self.assertIn("e->n >= FD_SEN_TOK", sync)
        self.assertIn("e->n < 3", sync)
        self.assertIsNone(re.search(r"e->n\s*<\s*9", sync))
        self.assertIsNone(re.search(r"e->n\s*!=\s*9", sync))
        # so cau moi tu van la 9, khong doi thanh 15
        self.assertRegex(sync, r"const char \*sen\[9\]")
        self.assertRegex(sync, r"line_sents\([^\n]*9\)")

    def test_listen_keyboard_and_meaning(self):
        listen = read("components/fd_ui/scr_listen.c")
        self.assertIn("fd_t9kb_set_qwerty_listen", listen)
        self.assertNotIn("fd_t9kb_set_qwerty_plain", listen)
        self.assertIn("HEAR_MEANING", listen)
        self.assertIn("meaning_show", listen)
        self.assertIn("3000", listen)
        self.assertIn("LV_FLEX_FLOW_ROW_WRAP", listen)
        kb = read("components/fd_ui/fd_t9kb.c")
        self.assertIn("fd_t9kb_set_qwerty_plain", kb)
        self.assertIn("fd_t9kb_set_qwerty_listen", kb)

    def test_sentence_tiles_wrap(self):
        sentence = read("components/fd_ui/scr_sentence.c")
        self.assertIn("LV_FLEX_FLOW_ROW_WRAP", sentence)
        self.assertIn("NT > 9", sentence)

    def test_i18n_meaning_key(self):
        vi = read("i18n/vi.yml")
        en = read("i18n/en.yml")
        i18n = read("components/fd_i18n/lv_i18n.c")
        self.assertIn("HEAR_MEANING", vi)
        self.assertIn("HEAR_MEANING", en)
        self.assertIn('"HEAR_MEANING"', i18n)
        self.assertIn("Nghĩa", vi)
        self.assertIn("Meaning", en)


if __name__ == "__main__":
    unittest.main()
