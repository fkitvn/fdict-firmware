"""Default UI font must be Vietnamese-capable Noto 14 (fd_font_noto_14), Montserrat 14 only as symbol fallback."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

def text(p):
    return (ROOT / p).read_text(encoding="utf-8")

def font_codepoints(name):
    s = text("components/fd_fonts/" + name + ".c")
    def nums(ident):
        m = re.search(r"\b" + ident + r"\[\]\s*=\s*\{(.*?)\};", s, re.S)
        if not m:
            return []
        body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
        return [int(x, 0) for x in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", body)]
    glyphs = set()
    parts = s.split("cmaps[]", 1)
    if len(parts) < 2:
        return glyphs
    cmap = parts[1].split("/*-----------------", 1)[0]
    for start, length, gid, ul in re.findall(
        r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*\.unicode_list = (\w+)",
        cmap,
    ):
        offsets = range(int(length)) if ul == "NULL" else nums(ul)
        for v in offsets:
            glyphs.add(int(start) + v)
    return glyphs

def collect_chars(*paths):
    chars = set()
    for p in paths:
        raw = text(p)
        for ch in raw:
            o = ord(ch)
            if o >= 32 and ch not in "\t\n\r":
                chars.add(ch)
    return chars

class FontDefaultGuard(unittest.TestCase):
    def test_lv_font_default_is_noto_14(self):
        header = text("components/fd_fonts/include/fd_lv_font_default.h")
        self.assertIn("#define LV_FONT_DEFAULT (&fd_font_noto_14)", header)
        self.assertIn("#define LV_FONT_CUSTOM_DECLARE extern const lv_font_t fd_font_noto_14;", header)
        cmake = text("CMakeLists.txt")
        self.assertIn("fd_lv_font_default.h", cmake)
        self.assertIn("SHELL:-include", cmake)
        defaults = text("sdkconfig.defaults")
        self.assertIn("CONFIG_LV_USE_CUSTOM_FONT_DEFAULT=y", defaults)
        self.assertIn("CONFIG_LV_FONT_USE_CUSTOM_INCLUDE=y", defaults)
        self.assertIn('CONFIG_LV_FONT_CUSTOM_INCLUDE="fd_lv_font_default.h"', defaults)
        self.assertIn("CONFIG_LV_FONT_MONTSERRAT_14=y", defaults)
        # sdkconfig la file sinh ra (gitignore): chi kiem tra khi co (may da build).
        cfg_path = ROOT / "sdkconfig"
        if cfg_path.exists():
            cfg = cfg_path.read_text(encoding="utf-8")
            self.assertIn("CONFIG_LV_USE_CUSTOM_FONT_DEFAULT=y", cfg)
            self.assertNotIn("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y", cfg)

    def test_no_wrapper_font_and_noto14_falls_back_to_montserrat(self):
        # Font boc co .dsc = NULL lam LVGL 9 crash khi lay bitmap (resolved_font = font boc).
        old = "fd_font_" + "default"
        self.assertFalse((ROOT / "components/fd_fonts" / (old + ".c")).exists())
        for p in list((ROOT / "components").rglob("*.c")) + list((ROOT / "components").rglob("*.h")) + \
                list((ROOT / "components").rglob("CMakeLists.txt")):
            self.assertNotIn(old, p.read_text(encoding="utf-8", errors="ignore"), str(p))
        noto = text("components/fd_fonts/fd_font_noto_14.c")
        desc = noto.split("fd_font_noto_14 = {", 1)[1].split("};", 1)[0]
        self.assertIn(".fallback = &lv_font_montserrat_14", desc)
        self.assertIn(".dsc = &font_dsc", desc)
        self.assertIn("--lv-fallback lv_font_montserrat_14", text("tools/gen_fonts.sh"))
        # Moi font fd_* deu la font fmt_txt that (co dsc), khong co font boc.
        for p in (ROOT / "components/fd_fonts").glob("fd_font_*.c"):
            s = p.read_text(encoding="utf-8")
            self.assertIn(".dsc = &font_dsc", s, p.name)
            self.assertIn("lv_font_get_bitmap_fmt_txt", s, p.name)

    def test_helpers_always_set_noto(self):
        priv = text("components/fd_ui/ui_priv.h")
        self.assertIn("lv_obj_t *ui_label_create(", priv)
        self.assertIn("lv_obj_t *ui_dialog_open(void)", priv)
        widgets = text("components/fd_ui/ui_widgets.c")
        helper = widgets.split("lv_obj_t *ui_label_create(")[1].split("\n}", 1)[0]
        self.assertIn("&fd_font_noto_14", helper)
        self.assertIn("lv_obj_set_style_text_font", helper)
        dlg = widgets.split("lv_obj_t *ui_dialog_open(void)")[1].split("\n}", 1)[0]
        self.assertIn("lv_layer_top()", dlg)
        self.assertIn("lv_obj_remove_style_all(ov)", dlg)
        self.assertIn("&fd_font_noto_14", dlg)
        self.assertIn("lv_obj_set_style_text_font", dlg)
        btn = widgets.split("lv_obj_t *ui_dialog_btn(")[1].split("\n}", 1)[0]
        self.assertIn("&fd_font_noto_14", btn)
        nav = text("components/fd_ui/ui_nav.c")
        self.assertIn("lv_obj_set_style_text_font(lv_layer_top(), &fd_font_noto_14, 0)", nav)
        self.assertIn("lv_obj_set_style_text_font(lv_layer_sys(), &fd_font_noto_14, 0)", nav)
        self.assertIn("&fd_font_noto_14", nav.split("lv_theme_default_init")[1].split(";", 1)[0])

    def test_dialogs_use_helper_or_explicit_font_on_layers(self):
        ui = ROOT / "components/fd_ui"
        raw_creates = []
        missing_font = []
        for p in sorted(ui.glob("*.c")):
            s = p.read_text(encoding="utf-8")
            # skip the helper definition itself
            for i, line in enumerate(s.splitlines(), 1):
                if "lv_obj_create(lv_layer_top())" in line or "lv_obj_create(lv_layer_sys())" in line:
                    if p.name == "ui_widgets.c" and "ui_dialog_open" in s[max(0, s.find(line)-200):s.find(line)+80]:
                        continue
                    raw_creates.append(f"{p.name}:{i}")
                if "lv_label_create(lv_layer_top())" in line or "lv_label_create(lv_layer_sys())" in line:
                    window = "\n".join(s.splitlines()[max(0, i-1): i+8])
                    if "set_style_text_font" not in window and "ui_label_create" not in window:
                        missing_font.append(f"{p.name}:{i}")
        # Overlays that are not text dialogs may still create on layer_top; they must not
        # create labels without a font. Dialogs should go through ui_dialog_open.
        self.assertEqual(missing_font, [])
        self.assertIn("ui_dialog_open", text("components/fd_ui/scr_spell.c"))
        self.assertIn("ui_dialog_open", text("components/fd_ui/scr_listen.c"))
        self.assertIn("ui_dialog_open", text("components/fd_ui/scr_sentence.c"))

    def test_vietnamese_glyphs_in_noto_14_and_18(self):
        used = collect_chars("i18n/vi.yml", "components/fd_i18n/lv_i18n.c")
        # Skip markup/punctuation already in ASCII; keep letters and typographic marks.
        need = {ch for ch in used if ord(ch) > 127}
        noto14 = font_codepoints("fd_font_noto_14")
        noto18 = font_codepoints("fd_font_noto_18")
        miss14 = sorted((ch for ch in need if ord(ch) not in noto14), key=ord)
        miss18 = sorted((ch for ch in need if ord(ch) not in noto18), key=ord)
        self.assertEqual(miss14, [], "Noto 14 missing: " + " ".join(f"U+{ord(c):04X} {c}" for c in miss14))
        self.assertEqual(miss18, [], "Noto 18 missing: " + " ".join(f"U+{ord(c):04X} {c}" for c in miss18))
        noto12 = font_codepoints("fd_font_noto_12")
        miss12 = sorted((ch for ch in need if ord(ch) not in noto12), key=ord)
        self.assertEqual(miss12, [], "Noto 12 missing: " + " ".join(f"U+{ord(c):04X} {c}" for c in miss12))
        title32 = font_codepoints("fd_font_noto_32_title")
        title_have = title32 | noto18  # .fallback = noto_18: chu thieu (vd é trong Réglages) ve Noto 18, khong o vuong
        missing32 = sorted((ch for ch in need if ord(ch) not in title32), key=ord)
        # Title font is a small subset; record but only fail strings actually drawn with it.
        uses = []
        for p in (ROOT / "components/fd_ui").glob("*.c"):
            s = p.read_text(encoding="utf-8")
            if "fd_font_noto_32_title" in s:
                uses.append(p.name)
        print("Noto 32 title is a subset font; missing vs all UI chars:",
              " ".join(f"U+{ord(c):04X}" for c in missing32[:40]),
              "..." if len(missing32) > 40 else "",
              "used in", ", ".join(uses))
        # Chuoi tieu de qua ui_screen_title(..., _("KEY")) ve bang Noto 32 title (font con): moi ky tu phai co.
        c = text("components/fd_i18n/lv_i18n.c")
        miss = []
        for p in (ROOT / "components/fd_ui").glob("*.c"):
            for key in re.findall(r'ui_screen_title\([^;]*?_\("([A-Z0-9_]+)"\)', p.read_text(encoding="utf-8")):
                for val in re.findall(r'\{"' + key + r'", "([^"]*)"\}', c):
                    miss += ["%s %r U+%04X" % (key, val, ord(ch)) for ch in val if ord(ch) not in title_have]
        self.assertEqual(miss, [], "Noto 32 title thieu glyph cho tieu de")

    def test_listen_hides_keyboard_only_when_solved(self):
        s = text("components/fd_ui/scr_listen.c")
        self.assertIn("kb_set_visible(false)", s)
        self.assertIn("if (s_solved) kb_set_visible(false)", s)
        err = s.split("static void slots_error(void)")[1].split("static void report_current", 1)[0]
        self.assertNotIn("kb_set_visible(false)", err)
        self.assertNotIn("kb_enable(false)", err)
        self.assertIn("kb_focus_input()", s.split("static void shake_done")[1].split("static void shake_cb", 1)[0])
        show = s.split("static void show_question(void)\n{")[1].split("static void load_task", 1)[0]
        self.assertIn("kb_set_visible(true)", show)

if __name__ == "__main__":
    unittest.main(verbosity=2)
