"""3.4.9 safety contracts. Source checks complement the clean target build."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]
def text(p): return (ROOT / p).read_text(encoding="utf-8")

class MemorySafety(unittest.TestCase):
    def test_json_hooks_installed_before_consumers(self):
        s = text("main/app_main.c").split("void app_main(void)")[1]
        self.assertLess(s.index("fd_json_memory_init()"), s.index("nvs_init()"))
        hooks = text("main/json_memory.c")
        self.assertIn("cJSON_InitHooks(&hooks)", hooks)
        self.assertIn("MALLOC_CAP_SPIRAM", hooks)
        self.assertIn(".free_fn = heap_caps_free", hooks)
    def test_large_buffers_no_internal_fallback(self):
        for p in ["components/fd_ota/fd_sync.c", "components/fd_ota/fd_dictnet.c"]:
            s = text(p).split("static void *ps_alloc(size_t n)")[1].split("\n}", 1)[0]
            self.assertIn("MALLOC_CAP_SPIRAM", s)
            self.assertNotIn("malloc(n)", s)
        self.assertNotIn("if (!b) b = malloc", text("components/fd_ota/fd_deck.c"))
    def test_tables_and_cards_not_duplicated_in_dram(self):
        s = text("components/fd_i18n/lv_i18n.c")
        for lang in ["en", "vi"]:
            self.assertIn("static const lv_i18n_phrase_t " + lang + "_singulars", s)
        ui = text("components/fd_ui/scr_flashcard.c")
        self.assertIn("s_items = s_buf", ui)
        self.assertNotIn("s_net[", ui)
    def test_init_transactional_unwind(self):
        for p in ["components/fd_ota/fd_ota.c", "components/fd_ota/fd_dictnet.c"]:
            s = text(p)
            self.assertIn("ulTaskNotifyTake(pdTRUE, portMAX_DELAY)", s)
            self.assertIn("xTaskNotifyGive(worker)", s)
            self.assertIn("vQueueDelete(queue)", s)
            self.assertIn("vSemaphoreDelete(mx)", s)
    def test_storage_never_autoformatted(self):
        self.assertIn(".format_if_mount_failed = false", text("components/fd_store/fd_store_fs.c"))
        uses = [p.name for p in (ROOT / "components").rglob("*.c")
                if "esp_littlefs_format(" in p.read_text(encoding="utf-8")]
        self.assertEqual(uses, ["fd_factory.c"])
        s = text("components/fd_store/fd_factory.c")
        self.assertLess(s.index("if (v != 1) return"), s.index("esp_littlefs_format("))
    def test_dictionary_preserves_files_on_failure(self):
        s = text("components/fd_ota/fd_dictnet.c")
        self.assertNotIn("unlink(HIST_PATH);", s)
        self.assertNotIn("unlink(Q_PATH);", s)
        save = s.split("static fd_dictnet_err_t hist_save(void)")[1].split("static fd_dictnet_err_t hist_push", 1)[0]
        self.assertNotIn("continue;", save)
        self.assertIn("!cJSON_AddItemToArray(a, o)", save)
        atomic = s.split("static fd_dictnet_err_t write_atomic")[1].split("static cJSON *entry_json", 1)[0]
        for check in ["fwrite(", "fflush(f)", "fsync(fileno(f))", "fclose(f)", "rename(tmp, path)"]:
            self.assertIn(check, atomic)
        flush = s.split("static void flush_queue(void)")[1].split("static void do_lookup", 1)[0]
        self.assertIn("if (!o) { complete = false; break; }", flush)
        self.assertIn("if (kn + len >= KEEP_CAP)", flush)
        self.assertIn("if (complete && write_atomic", flush)
    def test_single_card_decks_and_oom_distinct(self):
        s = text("components/fd_ota/fd_sync.c").split("int fd_sync_build_cards(")[1].split("/* Tach cau", 1)[0]
        self.assertIn("!s_mx", s)
        self.assertNotIn("fd_sync_ready()", s)  # quiz needs four words; flashcards do not
        self.assertIn("if (!o) return -1", s)
    def test_errors_and_heap_observable(self):
        for lang in ["en", "vi"]:
            for key in ["MEMORY_ERROR", "STORAGE_ERROR"]:
                self.assertIn(key + ":", text("i18n/" + lang + ".yml"))
        self.assertIn('ESP_LOGI("heap", "about: internal free=%u largest=%u min=%u PSRAM free=%u B"', text("components/fd_ui/scr_about.c"))
        self.assertIn("esp_timer_start_periodic", text("main/app_main.c"))

if __name__ == "__main__": unittest.main(verbosity=2)
