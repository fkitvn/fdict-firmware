"""Pending login/warranty UI changes: source order and real font-metric budgets."""
import re
import unittest
from test_release_ui import Font, text

class LoginWarrantyLayout(unittest.TestCase):
    def test_instruction_qr_countdown_buttons_source_order(self):
        s = text("components/fd_ui/scr_pair.c")
        build = s.split("static void build(lv_obj_t *scr)")[1]
        order = [build.index(x) for x in ["s_help = center_lbl", "s_qr_wrap = lv_obj_create", "s_msg = center_lbl", "lv_obj_t *row = lv_obj_create(col)"]]
        self.assertEqual(order, sorted(order))
        code = s.split("case FD_PAIR_CODE:")[1].split("case FD_PAIR_ERROR:")[0]
        self.assertIn('help = _("PAIR_SCAN_WITH_PHONE")', code)
        self.assertIn('snprintf(b, sizeof(b), "%d:%02d"', code)
        self.assertIn("lv_obj_set_size(sp, 1, 0)", build)
        for p in ["i18n/en.yml", "i18n/vi.yml"]:
            line = next(x for x in text(p).splitlines() if "PAIR_SCAN_WITH_PHONE:" in x)
            self.assertNotIn("%", line)
    def test_login_landscape_and_portrait_pixel_budget(self):
        font = Font("fd_font_noto_14")
        line_height = int(re.search(r"\.line_height = (\d+)", text("components/fd_fonts/fd_font_noto_14.c")).group(1))
        def lines(value, width):
            rows = [""]
            for word in value.split():
                candidate = (rows[-1] + " " + word).strip()
                if rows[-1] and font.width(candidate) > width: rows.append(word)
                else: rows[-1] = candidate
            return len(rows)
        for width, height, qr, padding in [(320,240,100,8), (240,320,135,12)]:
            for phrase in ["Quét bằng điện thoại để đăng nhập", "Scan with your phone to sign in"]:
                count = lines(phrase, width - 24)
                total = count * line_height + qr + 26 + line_height + 44 + 4 * 4 + padding
                self.assertLessEqual(total, height)
                self.assertLessEqual(font.width("10:00"), width-24)
                print(f"login {width}x{height}: {count} instruction line(s), {total}/{height}px; QR {qr}px")
    def test_warranty_device_id_unconditional_and_matches_qr_source(self):
        s = text("components/fd_ui/scr_warranty.c")
        build = s.split("static void build(lv_obj_t *scr)")[1]
        self.assertIn("ui_device_ident(device_id, sizeof(device_id))", build)
        self.assertIn('lv_label_set_text_fmt(device, "%s: %s", _("DEVICE_ID"), device_id)', build)
        fill = s.split("static void fill(void)")[1].split("static void retry_cb")[0]
        self.assertNotIn("lv_obj_set_hidden(device", fill)
        self.assertNotIn("ui_device_ident", fill)  # computed once, never tied to API loading/error
        qr = text("components/fd_ui/scr_pair.c").split("static void build_qr_url(")[1].split("/* 1 ky tu",1)[0]
        self.assertIn("ui_device_ident(id, sizeof(id))", qr)
        font = Font("fd_font_noto_14")
        for width in [296, 216]:
            self.assertLessEqual(font.width("Mã thiết bị: 84c7bb78c178"), width)
    def test_canonical_qr_url_keeps_metadata(self):
        for p in ["components/fd_ota/Kconfig", "scripts/ci-ota-build.sh"]:
            self.assertIn("/device-login", text(p))
            self.assertNotIn("/pairing-device", text(p))
        qr = text("components/fd_ui/scr_pair.c").split("static void build_qr_url(")[1].split("/* 1 ky tu", 1)[0]
        self.assertIn('"%s?id=%s"', qr)
        for key in ["hw", "fw", "b"]:
            self.assertIn('q_add(u, n, "' + key + '"', qr)
    def test_warranty_qr_state_still_fits_landscape(self):
        # Conservative two-line guide budget, including the new ID, no QR shrink.
        total = 24 + 20 + 20 + 40 + 96 + 24 + 4*2 + 8
        self.assertEqual(total, 240)
        self.assertIn("lv_obj_set_style_pad_row(col, 2, 0)", text("components/fd_ui/scr_warranty.c"))
        self.assertIn("lv_obj_set_style_pad_ver(col, 4, 0)", text("components/fd_ui/scr_warranty.c"))
        print(f"warranty landscape conservative 2-line guide + device ID: {total}/240px")

if __name__ == "__main__": unittest.main(verbosity=2)
