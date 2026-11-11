"""Host source/layout regression checks; no device, serial, or network required."""
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[2]
def text(p): return (ROOT / p).read_text(encoding="utf-8")

class MemoryRegression(unittest.TestCase):
    def test_psram_buffers(self):
        for p, declaration in [("components/fd_ota/fd_dictnet.c", "s_hist"), ("components/fd_ui/scr_dict.c", "hist"), ("components/fd_ui/ui_widgets.c", "s_vb_pcm")]:
            self.assertRegex(text(p), r"static EXT_RAM_BSS_ATTR \w+ " + declaration + r"\[")
    def test_dma_budget(self):
        s = text("components/board_fnk0104b/include/board_orient.h")
        self.assertRegex(s, r"BOARD_DRAW_BUF_LINES 16\b")
        self.assertEqual(2 * 320 * (40 - 16) * 2, 30720)
    def test_config(self):
        self.assertIn("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=256", text("sdkconfig.defaults.esp32s3"))
        self.assertIn("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=49152", text("sdkconfig.defaults.esp32s3"))
    def test_bounded_retry_source(self):
        s = text("components/fd_net/wifi_mgr.c")
        self.assertNotIn("schedule_ms(RETRY_TARGET, 800)", s)
        self.assertIn("s_state = FD_NET_ERROR", s)
        self.assertIn("wifi_api_retry_ms(++s_api_failures)", s)
        self.assertIn("job.generation == s_generation", s)
        self.assertIn("heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)", s)
    def test_manual_budget_reset(self):
        s = text("components/fd_net/wifi_mgr.c")
        for name in ["fd_net_connect(", "fd_net_connect_saved("]:
            section = s.split("esp_err_t " + name)[1].split("\n}", 1)[0]
            self.assertIn("s_api_failures = 0", section)
            self.assertIn("s_last_error = ESP_OK", section)
    def test_task_failure_unwind(self):
        for p in ["components/fd_audio/fd_audio.c", "components/fd_ota/fd_sync.c", "components/fd_ota/fd_deck.c", "components/fd_voice/fd_voice.c"]:
            s = text(p)
            self.assertIn("vQueueDelete(s_q)", s)
            self.assertIn("s_q = NULL", s)
    def test_no_dead_streaming(self):
        for p in (ROOT / "components").rglob("*.c"):
            self.assertNotIn("fd_audio_stream_", p.read_text(encoding="utf-8"))
    def test_placeholder_ids_gone(self):
        s = text("components/fd_ui/include/fd_ui.h")
        for k in ["PICWORD", "FILLCHAR", "PRON", "REPEAT"]:
            self.assertNotIn("FD_SCR_" + k, s)
    def test_utf8_no_bom(self):
        for p in ["i18n/en.yml", "i18n/vi.yml", "components/fd_i18n/lv_i18n.c", "components/fd_ui/scr_pair.c"]:
            b = (ROOT / p).read_bytes()
            self.assertFalse(b.startswith(b"\xef\xbb\xbf"))
            b.decode("utf-8", errors="strict")
    def test_login_translations(self):
        for p in ["i18n/vi.yml", "components/fd_i18n/lv_i18n.c"]:
            s = text(p)
            self.assertIn("Đang lấy mã đăng nhập", s)
            self.assertIn("Quét bằng điện thoại để đăng nhập", s)
        self.assertIn("char b[128]", text("components/fd_ui/scr_pair.c"))
    def test_button_disabled_before_request(self):
        s = text("components/fd_ui/scr_pair.c").split("static void pair_cb(")[1].split("/* ----",1)[0]
        self.assertLess(s.index("lv_obj_add_state(s_btn, LV_STATE_DISABLED)"), s.index("fd_ota_pair_start()"))
        self.assertNotIn("fd_ota_pair_stop()", s)
        ota = text("components/fd_ota/fd_ota.c")
        self.assertIn("if (s_pair_start_pending) out->state = FD_PAIR_REQUESTING", ota)
        self.assertIn('strlcpy(s_pair.err, "busy"', ota)
        ui = text("components/fd_ui/scr_pair.c")
        self.assertIn("lv_obj_set_state(s_btn, LV_STATE_DISABLED, p.state == FD_PAIR_REQUESTING && !s_nowifi)", ui)
    def test_main_stack_not_retained_for_diagnostics(self):
        main = text("main/app_main.c")
        self.assertIn("esp_timer_start_periodic", main)
        self.assertNotIn("while (1)", main)
    def test_warranty_fetch_only_on_details(self):
        for p in ["main/app_main.c", "components/fd_ui/scr_settings.c", "components/fd_ui/scr_pair.c", "components/fd_ui/scr_about.c"]:
            self.assertNotIn("fd_warranty_", text(p))
        about = text("components/fd_ui/scr_about.c")
        self.assertNotIn("R_WARR", about)
        self.assertNotIn("WARRANTY", about)
        settings = text("components/fd_ui/scr_settings.c")
        self.assertIn('_("WARRANTY_INFO"), warr_cb, NULL, NULL', settings)
        self.assertNotIn("s_warr_sub", settings)
    def test_warranty_readonly_api_and_errors(self):
        worker = text("components/fd_ota/fd_warranty.c")
        self.assertIn('"/api/devices/warranty/lookup"', worker)
        for old in ["wait_wifi(90)", "for (;;)"]:
            self.assertNotIn(old, worker)
        for state in ["FD_WARR_LOADING", "FD_WARR_READY", "FD_WARR_ERROR"]:
            self.assertIn(state, worker)
        self.assertIn("vTaskDelete(NULL)", worker)
        self.assertIn("cJSON_IsBool(activated)", worker)
    def test_login_font_width_and_height(self):
        f = text("components/fd_fonts/fd_font_noto_14.c")
        def numbers(name):
            v = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\};", f, re.S).group(1)
            v = re.sub(r"/\*.*?\*/", "", v, flags=re.S)
            return [int(x, 0) for x in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", v)]
        advances = [int(v) for v in re.findall(r"\.adv_w = (\d+)", f)]
        cmap = f.split("cmaps[]",1)[1].split("/*-----------------",1)[0]
        glyphs = {}
        for start,length,gid,ul in re.findall(r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*\.unicode_list = (\w+)", cmap):
            offsets = range(int(length)) if ul == "NULL" else numbers(ul)
            for i,v in enumerate(offsets): glyphs[int(start)+v] = int(gid)+i
        left, right, kern = numbers("kern_left_class_mapping"), numbers("kern_right_class_mapping"), numbers("kern_class_values")
        def width(s):
            ids=[glyphs[ord(c)] for c in s]; total=0
            for i,g in enumerate(ids):
                k=0
                if i+1 < len(ids) and left[g] and right[ids[i+1]]:
                    k=kern[(left[g]-1)*33+right[ids[i+1]]-1]
                total += (advances[g]+k+8)//16
            return total
        for w,h,qr in [(320,240,100),(240,320,135)]:
            for phrase in ["Đang lấy mã đăng nhập…", "Quét bằng điện thoại để đăng nhập · 10:00", "Getting login code…", "Scan with your phone to sign in · 10:00"]:
                lines=[""]
                for word in phrase.split():
                    candidate=(lines[-1]+" "+word).strip()
                    if width(candidate)>w-24 and lines[-1]: lines.append(word)
                    else: lines[-1]=candidate
                self.assertLessEqual(len(lines),2,(w,phrase,lines))
                self.assertLessEqual(qr+26+len(lines)*20+44+12+(8 if h==240 else 12),h)
                print(f"layout {w}x{h}: {width(phrase)}px -> {len(lines)} line(s): {ascii(phrase)}")


if __name__ == "__main__": unittest.main(verbosity=2)