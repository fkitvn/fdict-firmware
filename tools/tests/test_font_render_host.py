"""Ve chu Viet + LV_SYMBOL_* bang font mac dinh tren may host (gcc + LVGL trong managed_components).
Bat loi crash cua font boc (4.5.4). Bo qua neu khong co gcc (hoac WSL co gcc tren Windows) hay chua co managed_components
(chay idf.py build mot lan). Tat: FD_SKIP_HOST_RENDER=1. Object LVGL cache o build/host_render."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
LVGL = ROOT / "managed_components" / "lvgl__lvgl"
WORK = ROOT / "build" / "host_render"


def _runner():
    """Tra ve (ham doi duong dan, tien to lenh) hoac None neu khong co trinh bien dich host."""
    if os.name != "nt":
        if shutil.which(os.environ.get("CC", "gcc")) and shutil.which("python3"):
            return (lambda p: str(p)), ["python3"]
        return None
    if not shutil.which("wsl"):
        return None
    try:
        ok = subprocess.run(["wsl", "-e", "sh", "-c", "command -v gcc && command -v python3"],
                            capture_output=True, timeout=60).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return None
    if not ok:
        return None

    def conv(p):
        r = subprocess.run(["wsl", "-e", "wslpath", "-a", str(p)], capture_output=True, text=True, timeout=30)
        return r.stdout.strip()

    return conv, ["wsl", "-e", "python3"]


class HostFontRender(unittest.TestCase):
    def test_default_font_renders_vietnamese_and_symbols(self):
        if os.environ.get("FD_SKIP_HOST_RENDER") == "1":
            self.skipTest("FD_SKIP_HOST_RENDER=1")
        if not (LVGL / "src" / "font" / "lv_font.c").exists():
            self.skipTest("chua co managed_components/lvgl__lvgl (chay idf.py build truoc)")
        run = _runner()
        if not run:
            self.skipTest("khong co gcc host (Linux/macOS) hoac WSL co gcc + python3 (Windows)")
        conv, cmd = run
        WORK.mkdir(parents=True, exist_ok=True)
        script = ROOT / "tools" / "tests" / "host_render" / "build_run.py"
        r = subprocess.run(cmd + [conv(script), conv(ROOT), conv(LVGL), conv(WORK)],
                           capture_output=True, text=True, timeout=1200)
        out = (r.stdout or "") + (r.stderr or "")
        if r.returncode == 2:
            self.fail("khong bien dich duoc test host:\n" + out[-3000:])
        self.assertEqual(r.returncode, 0, out[-3000:])
        self.assertIn("RENDER_OK", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
