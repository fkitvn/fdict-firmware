"""Bien dich LVGL (managed_components) + font fd_* + main.c bang gcc host roi chay. Goi tu test_font_render_host.py
(truc tiep tren Linux/macOS, hoac trong WSL tren Windows). Doi so: <repo> <lvgl> <work>. In RENDER_OK neu dat."""
import concurrent.futures as cf
import hashlib
import os
import subprocess
import sys

repo, lvgl, work = sys.argv[1:4]
here = os.path.join(repo, "tools/tests/host_render")
hdr = os.path.join(repo, "components/fd_fonts/include/fd_lv_font_default.h")
cc = os.environ.get("CC", "gcc")
SKIP = ("/drivers/", "/libs/", "/draw/nxp", "/draw/vg_lite", "/draw/renesas", "/draw/dma2d", "/draw/opengles",
        "/draw/sdl", "/draw/espressif", "/draw/eve", "/draw/nema", "/3d", "/debugging/test")
flags = ["-O0", "-w", "-DLV_CONF_INCLUDE_SIMPLE", "-I" + here, "-I" + lvgl, "-I" + os.path.join(lvgl, "include"),
         "-I" + os.path.join(repo, "components/fd_fonts/include"), "-include", hdr]

srcs = []
for d, _, fs in os.walk(os.path.join(lvgl, "src")):
    for f in fs:
        p = os.path.join(d, f).replace("\\", "/")
        if f.endswith(".c") and not any(s in p for s in SKIP):
            srcs.append(p)
srcs.append(os.path.join(lvgl, "src/libs/rle/lv_rle.c"))
srcs.sort()

# Cache object LVGL theo phien ban LVGL + lv_conf + header font mac dinh + co bien dich.
key = hashlib.sha256()
for p in (os.path.join(lvgl, "idf_component.yml"), os.path.join(here, "lv_conf.h"), hdr):
    if os.path.exists(p):
        key.update(open(p, "rb").read())
key.update(" ".join(flags).encode())
odir = os.path.join(work, "lvgl-" + key.hexdigest()[:12])
os.makedirs(odir, exist_ok=True)


def comp(src):
    obj = os.path.join(odir, hashlib.md5(src.encode()).hexdigest()[:16] + ".o")
    if not os.path.exists(obj):
        r = subprocess.run([cc, "-c", *flags, src, "-o", obj + ".tmp"], capture_output=True, text=True)
        if r.returncode:
            return src + "\n" + r.stderr[-2000:]
        os.replace(obj + ".tmp", obj)
    return None


with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
    errs = [e for e in ex.map(comp, srcs) if e]
if errs:
    print("BUILD_FAIL lvgl:\n" + errs[0])
    sys.exit(2)

objs = [os.path.join(odir, hashlib.md5(s.encode()).hexdigest()[:16] + ".o") for s in srcs]
fonts_dir = os.path.join(repo, "components/fd_fonts")
own = [os.path.join(fonts_dir, f) for f in sorted(os.listdir(fonts_dir)) if f.startswith("fd_font_") and f.endswith(".c")]
own.append(os.path.join(here, "main.c"))
exe = os.path.join(work, "font_render")
# main.c: khong cho goi ham chua khai bao (vd ham private LVGL) - voi -w gcc 13 chi canh bao roi crash luc chay.
strict = [f for f in flags if f != "-w"] + ["-Werror=implicit-function-declaration"]
r = subprocess.run([cc, "-fsyntax-only", *strict, os.path.join(here, "main.c")], capture_output=True, text=True)
if r.returncode:
    print("BUILD_FAIL main.c:\n" + r.stderr[-3000:])
    sys.exit(2)
r = subprocess.run([cc, *flags, *own, *objs, "-lm", "-o", exe], capture_output=True, text=True)
if r.returncode:
    print("BUILD_FAIL link:\n" + r.stderr[-3000:])
    sys.exit(2)
data = os.path.join(repo, "tools/tests/data")
r = subprocess.run([exe, os.path.join(data, "ipa_samples.txt"), os.path.join(data, "vi_samples.txt")],
                   capture_output=True, text=True, timeout=60)
sys.stdout.write(r.stdout)
if r.returncode:
    print("RUN_FAIL exit=%d (am = tin hieu, -11 = SIGSEGV)" % r.returncode)
    sys.exit(1)
