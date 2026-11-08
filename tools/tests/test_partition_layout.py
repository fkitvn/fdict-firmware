"""Partition/UI/release safety checks for the 4.0.0 development layout."""
from pathlib import Path
import csv
import unittest
ROOT = Path(__file__).resolve().parents[2]
def text(p): return (ROOT / p).read_text(encoding="utf-8")

class PartitionLayout(unittest.TestCase):
    def parts(self):
        rows = csv.reader(x for x in text("partitions.csv").splitlines() if x.strip() and not x.startswith("#"))
        return {r[0].strip(): (int(r[3].strip(), 0), int(r[4].strip(), 0)) for r in rows}
    def test_layout_fits_16mb_without_overlap(self):
        p = self.parts()
        self.assertEqual(p["nvs"], (0x9000, 0x10000))
        self.assertEqual(p["otadata"], (0x19000, 0x2000))
        self.assertEqual(p["phy_init"], (0x1B000, 0x1000))
        self.assertEqual(p["ota_0"], (0x20000, 0x300000))
        self.assertEqual(p["ota_1"], (0x320000, 0x300000))
        self.assertEqual(p["storage"], (0x620000, 0x810000))
        self.assertEqual(p["voice"], (0xE30000, 0x1C0000))
        end = 0x9000
        for off, size in sorted(p.values()):
            self.assertGreaterEqual(off, end)
            end = off + size
        self.assertEqual(end, 16 * 1024 * 1024)
        self.assertEqual(p["storage"][1] - 0x610000, 2 * 1024 * 1024)
    def test_ci_rejects_apps_not_fitting_3mb(self):
        self.assertIn("SIZE < 3145728", text("scripts/ci-ota-build.sh"))
        self.assertIn('test "$SIZE" -lt 3145728', text(".github/workflows/ota-prod.yml"))
    def test_littlefs_capacity_uses_mounted_partition(self):
        s = text("components/fd_ui/scr_about.c")
        self.assertIn('"LittleFS"', s)
        self.assertIn("fd_store_info(&tot, &used)", s)
        self.assertIn('"%u,%u / %u,%u MB"', s)
        self.assertIn("lv_label_set_text(s_val[R_LITTLEFS], o)", s)
    def test_migration_is_explicitly_usb_only(self):
        notes = text("CHANGELOG.md").split("## [3.4.8]")[0]
        self.assertIn("## [4.0.0]", notes)
        self.assertIn("USB", notes)
        self.assertIn("khôi phục cài đặt gốc", notes)
        self.assertIn("OTA replaces only the application", text(".github/workflows/ota-prod.yml"))

if __name__ == "__main__": unittest.main(verbosity=2)
