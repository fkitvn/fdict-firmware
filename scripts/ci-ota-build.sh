#!/usr/bin/env bash
# Build ban app OTA (ESP-IDF 6.1, esp32s3) cho CI va ghi OTA_BIN, FW_VERSION, BUILD_ID, NOTES_VI vao $GITHUB_ENV.
#
# - FW_VERSION: PROJECT_VER trong CMakeLists.txt goc, doi chieu voi esp_app_desc.version trong file bin.
# - BUILD_ID  : "fdictdm10" + 4 hex dau SHA256 file ELF (= esp_app_get_elf_sha256() tren may, man Thong tin),
#               doi chieu voi app_elf_sha256 trong esp_app_desc cua file bin.
# - NOTES_VI  : muc dau tien cua CHANGELOG.md (phai trung semver cua PROJECT_VER), van ban thuong, <= 500 ky tu.
#
# URL may chu KHONG nam trong repo: lay tu bien moi truong DICT_API_URL (GitHub secret) -> overlay sdkconfig tam
# trong thu muc build (khong commit, khong in ra log). Chay thu tai may khong co secret: ALLOW_PLACEHOLDER_URL=1.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_DIR="${BUILD_DIR:-build-ci}"
OUT_DIR="${OUT_DIR:-dist}"
HW="fnk0104b"

die() { echo "::error::$*" >&2; exit 1; }

# ---------- ESP-IDF ----------
if ! command -v idf.py >/dev/null 2>&1; then
  [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/export.sh" ]] || die "Khong tim thay ESP-IDF (IDF_PATH/export.sh)"
  # shellcheck disable=SC1091
  . "$IDF_PATH/export.sh" >/dev/null
fi
PY="$(command -v python3 || command -v python)"

# ---------- phien ban tu source ----------
FW_VERSION="$(sed -nE 's/^[[:space:]]*set\(PROJECT_VER[[:space:]]+"([^"]+)"\).*/\1/p' CMakeLists.txt | head -n1)"
[[ -n "$FW_VERSION" ]] || die "Khong doc duoc PROJECT_VER trong CMakeLists.txt"
[[ "$FW_VERSION" =~ ^FDICT-([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || die "PROJECT_VER sai dinh dang: $FW_VERSION"
SEMVER="${FW_VERSION#FDICT-}"

CL_TOP="$(sed -nE 's/^## \[([^]]+)\].*/\1/p' CHANGELOG.md | head -n1)"
[[ "$CL_TOP" == "$SEMVER" ]] || die "Muc dau CHANGELOG.md ($CL_TOP) khac PROJECT_VER ($SEMVER)"

NOTES_VI="$("$PY" - "$SEMVER" <<'PY'
import re, sys
ver = sys.argv[1]
lines = open("CHANGELOG.md", encoding="utf-8").read().splitlines()
out, on = [], False
for ln in lines:
    if ln.startswith("## ["):
        if on:
            break
        on = ln.startswith("## [" + ver + "]")
        continue
    if on and ln.strip():
        t = re.sub(r"^\s*[-*]\s+", "", ln.strip())
        t = re.sub(r"[`*_]", "", t)
        out.append(t)
s = " ".join(out).strip()
if not s:
    sys.exit("CHANGELOG.md: muc [%s] trong" % ver)
if "\ufffd" in s or re.search(r"(?<=\w)\?(?=\w)", s):
    sys.exit("CHANGELOG.md: muc [%s] co ky tu loi ma hoa (U+FFFD hoac ? giua chu) - hay luu file UTF-8" % ver)
print(s[:500].rstrip())
PY
)" || die "Khong lay duoc ghi chu tu CHANGELOG.md cho $SEMVER"
NOTES_VI="${NOTES_VI//$'\n'/ }"

# ---------- overlay URL may chu (khong in gia tri) ----------
# overlay URL nam NGOAI thu muc build (khong bi xoa khi cau hinh lai, khong lot vao artifact); xoa khi thoat
TMP_DIR="${RUNNER_TEMP:-$PWD/.ci-tmp}"
mkdir -p "$TMP_DIR"
TMP_DIR="$(cd "$TMP_DIR" && pwd)"
OVERLAY="$TMP_DIR/sdkconfig.ci"
trap 'rm -f "$OVERLAY"' EXIT
# URL may chu: DICT_BASE (buoc Mask da chuan hoa) hoac tu chuan hoa DICT_API_URL; khong in gia tri.
# shellcheck source=scripts/ci-url-normalize.sh
. scripts/ci-url-normalize.sh
if [[ -z "${DICT_BASE:-}" && -n "${DICT_API_URL:-}" ]]; then fd_norm_url || exit 1; fi
if [[ -z "${DICT_BASE:-}" && -n "${GITHUB_ACTIONS:-}" ]]; then fd_norm_url || exit 1; fi
if [[ -n "${DICT_BASE:-}" ]]; then
  base="$DICT_BASE"
  [[ "$base" =~ ^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$ ]] || die "DICT_BASE khong hop le"
  {
    echo "CONFIG_FD_OTA_BASE_URL=\"$base\""
    echo "CONFIG_FD_PAIR_WEB_URL=\"$base/pairing-device\""
    echo "CONFIG_FD_API_BASE_URL=\"$base/api/device/v1\""
  } > "$OVERLAY"
  echo "Overlay URL: CONFIG_FD_OTA_BASE_URL, CONFIG_FD_PAIR_WEB_URL, CONFIG_FD_API_BASE_URL (tu DICT_BASE)"
elif [[ "${ALLOW_PLACEHOLDER_URL:-0}" == "1" ]]; then
  : > "$OVERLAY"
  echo "CANH BAO: khong co DICT_API_URL -> dung URL placeholder (chi de thu build, KHONG phat hanh)"
else
  die "DICT_API_URL rong: chua dat secret hoac job thieu environment: production"
fi

# ---------- build ----------
rm -rf "$BUILD_DIR"   # luon build moi: sdkconfig sinh tu sdkconfig.defaults (+ .esp32s3 tu dong) + overlay
mkdir -p "$BUILD_DIR"
idf.py -B "$BUILD_DIR" -D IDF_TARGET=esp32s3 -D SDKCONFIG="$BUILD_DIR/sdkconfig" \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;$OVERLAY" build 2>&1 | tee "$BUILD_DIR/build.log"
if grep -E "(warning|error):" "$BUILD_DIR/build.log" >/dev/null; then
  grep -E "(warning|error):" "$BUILD_DIR/build.log" | head -n 20 >&2
  die "Build co canh bao/loi (yeu cau 0 canh bao)"
fi

APP_BIN="$BUILD_DIR/fdict-fw.bin"
ELF="$BUILD_DIR/fdict-fw.elf"
[[ -f "$APP_BIN" && -f "$ELF" ]] || die "Khong thay $APP_BIN / $ELF"

# ---------- BUILD_ID + doi chieu esp_app_desc trong bin ----------
ELF_SHA="$(sha256sum "$ELF" | awk '{print $1}')"
BUILD_ID="fdictdm10${ELF_SHA:0:4}"
[[ "$BUILD_ID" =~ ^fdictdm[0-9]{2}[0-9a-f]{4}$ ]] || die "BUILD_ID sai dinh dang: $BUILD_ID"

"$PY" - "$APP_BIN" "$FW_VERSION" "$ELF_SHA" <<'PY' || die "esp_app_desc trong bin khong khop source"
import struct, sys
path, want_ver, elf_sha = sys.argv[1], sys.argv[2], sys.argv[3]
b = open(path, "rb").read(0x20 + 256)
if b[0] != 0xE9:
    sys.exit("khong phai anh app ESP (magic)")
d = b[0x20:]
if struct.unpack_from("<I", d, 0)[0] != 0xABCD5432:
    sys.exit("khong thay esp_app_desc")
ver = d[16:48].split(b"\0")[0].decode()
sha = d[144:176].hex()
if ver != want_ver:
    sys.exit("version trong bin '%s' != PROJECT_VER '%s'" % (ver, want_ver))
if sha != elf_sha:
    sys.exit("app_elf_sha256 trong bin != SHA256 file ELF")
print("esp_app_desc: version=%s, elf_sha256 khop" % ver)
PY

SIZE="$(stat -c%s "$APP_BIN")"
(( SIZE > 0 && SIZE < 4194304 )) || die "Kich thuoc bin $SIZE khong hop le (< 4194304)"

# ---------- dau ra ----------
mkdir -p "$OUT_DIR"
OTA_BIN="$OUT_DIR/fdict-firmware-v${SEMVER}-${BUILD_ID}.bin"
cp "$APP_BIN" "$OTA_BIN"
case "$OTA_BIN" in *-full.bin) die "Cam day -full.bin len OTA" ;; esac
SHA256="$(sha256sum "$OTA_BIN" | awk '{print $1}')"
"$PY" - "$OUT_DIR/build-info.json" "$HW" "$FW_VERSION" "$BUILD_ID" "$SIZE" "$SHA256" "$ELF_SHA" "$NOTES_VI" <<'PY'
import json, sys
k = ["hw", "fw_version", "build_id", "size", "sha256", "elf_sha256", "notes_vi"]
v = sys.argv[2:]
d = dict(zip(k, v)); d["size"] = int(d["size"])
json.dump(d, open(sys.argv[1], "w", encoding="utf-8"), ensure_ascii=False, indent=2)
PY

# ---------- anh day du (-full.bin) tu CUNG ban build: offset 0, theo flash_args ----------
# bootloader 0x0 | partition-table 0x8000 | ota_data_initial 0x19000 | app (ota_0) 0x20000 | voice 0xE30000
FULL_BIN="$OUT_DIR/fdict-firmware-v${SEMVER}-${BUILD_ID}-full.bin"
[[ -f "$BUILD_DIR/flash_args" ]] || die "Khong thay $BUILD_DIR/flash_args"
grep -q "fdict-fw.bin" "$BUILD_DIR/flash_args" || die "flash_args thieu app"
FULL_ABS="$(cd "$OUT_DIR" && pwd)/$(basename "$FULL_BIN")"
( cd "$BUILD_DIR" && "$PY" -m esptool --chip esp32s3 merge-bin -o "$FULL_ABS" @flash_args ) \
  || die "merge-bin that bai"
FULL_SIZE="$(stat -c%s "$FULL_BIN")"
(( FULL_SIZE > 0 && FULL_SIZE <= 16777216 )) || die "Kich thuoc -full.bin $FULL_SIZE khong hop le (<= 16777216)"
FULL_SHA256="$(sha256sum "$FULL_BIN" | awk '{print $1}')"
echo "full: $(basename "$FULL_BIN") size=$FULL_SIZE sha256=$FULL_SHA256"

echo "fw_version=$FW_VERSION build_id=$BUILD_ID size=$SIZE sha256=$SHA256"

if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    echo "OTA_BIN=$OTA_BIN"
    echo "FW_VERSION=$FW_VERSION"
    echo "BUILD_ID=$BUILD_ID"
    echo "NOTES_VI=$NOTES_VI"
    echo "FULL_BIN=$FULL_BIN"
    echo "FULL_SHA256=$FULL_SHA256"
  } >> "$GITHUB_ENV"
fi
