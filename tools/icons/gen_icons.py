#!/usr/bin/env python3
"""Sinh icon C (LVGL 9) cho Fdict.

  * 9 o Home: Microsoft Fluent Emoji 3D (MIT) -> 48x48 RGB565A8 (Lanczos, alpha nhan truoc).
  * Icon nho: Material Symbols Rounded (Apache-2.0) -> A8 (to mau bang image_recolor).

Chay:  python3 tools/icons/gen_icons.py [--out components/fd_ui] [--preview icon-preview.png]
Can:   Python 3, Pillow, cairosvg, mang (tai tu raw.githubusercontent.com, co cache tools/icons/cache).
"""
import argparse, colorsys, io, os, urllib.parse, urllib.request
from PIL import Image
import cairosvg

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache")
FLUENT = "https://raw.githubusercontent.com/microsoft/fluentui-emoji/main/assets/"
MSYM = "https://raw.githubusercontent.com/google/material-design-icons/master/symbols/web/"

# (ten C, thu muc emoji Fluent, file 3D, nhan o Home, xu ly mau)
TILES = [
    ("img_tile_flashcard", "Card index", "card_index_3d.png", "Flash card", None),
    ("img_tile_study", "Graduation cap", "graduation_cap_3d.png", "Học tập", None),
    ("img_tile_picword", "Framed picture", "framed_picture_3d.png", "Đoán hình", None),
    ("img_tile_fillchar", "Input latin letters", "input_latin_letters_3d.png", "Điền chữ", None),
    ("img_tile_pron", "Speaker high volume", "speaker_high_volume_3d.png", "Phát âm", None),
    ("img_tile_repeat", "Studio microphone", "studio_microphone_3d.png", "Nhại từ", None),
    ("img_tile_sync", "Counterclockwise arrows button", "counterclockwise_arrows_button_3d.png", "Đồng bộ", None),
    ("img_tile_stats", "Bar chart", "bar_chart_3d.png", "Thống kê", None),
    ("img_tile_settings", "Gear", "gear_3d.png", "Cài đặt", "slate"),
]
TILE_PX = 48

# 16 px (bang nhanh) img_target "Bullseye", img_fire "Fire": sinh cung ham resize_premul/emit_rgb565a8 voi TILE_PX=16.
# (ten C, ten Material Symbol, kich thuoc px, mo ta)
SMALL = [
    ("img_rotate", "screen_rotation", 24, "screen_rotation"),
    ("img_sun", "light_mode", 20, "light_mode (mat troi)"),
    ("img_sleep", "bedtime", 24, "bedtime (mat trang / ngu)"),
]


def fetch(url, name):
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, name)
    if not os.path.exists(p):
        with urllib.request.urlopen(url, timeout=60) as r, open(p, "wb") as f:
            f.write(r.read())
    with open(p, "rb") as f:
        return f.read()


def recolor_slate(im):
    """Banh rang Fluent mau tim nhat (lavender) kho nhin tren nen sang: giu sac do (luminance) nhung
    chuyen sang tong xam xanh 'slate' (hue ~ #475569), keo dai tuong phan va lam toi ~30%."""
    px = im.load()
    w, h = im.size
    lums = [0.299 * px[x, y][0] + 0.587 * px[x, y][1] + 0.114 * px[x, y][2]
            for y in range(h) for x in range(w) if px[x, y][3] > 128]
    lums.sort()
    lo, hi = lums[int(len(lums) * 0.02)], lums[int(len(lums) * 0.98)]
    hue, _, sat = colorsys.rgb_to_hls(0x47 / 255, 0x55 / 255, 0x69 / 255)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            l = (0.299 * r + 0.587 * g + 0.114 * b - lo) / max(1, hi - lo)
            l = min(1.0, max(0.0, l))
            l = 0.18 + 0.60 * l          # dai sang 18%..78%: toi hon ban goc, van con khoi 3D
            rr, gg, bb = colorsys.hls_to_rgb(hue, l, min(1.0, sat * 1.1))
            px[x, y] = (round(rr * 255), round(gg * 255), round(bb * 255), a)
    return im


def resize_premul(im, size):
    """Lanczos tren alpha nhan truoc (tranh vien toi quanh mep trong suot)."""
    im = im.convert("RGBA")
    pm = im.convert("RGBa").resize((size, size), Image.LANCZOS)
    return pm.convert("RGBA")


def c_bytes(data, per_line):
    out = []
    for i in range(0, len(data), per_line):
        out.append("    " + ", ".join("0x%02x" % b for b in data[i:i + per_line]) + ",")
    return "\n".join(out)


def emit_rgb565a8(name, im, header, out_dir):
    w, h = im.size
    rgb, alpha = bytearray(), bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            rgb += bytes((v & 0xFF, v >> 8))   # little-endian native; esp_lvgl_port swap_bytes khi flush
            alpha.append(a)
    src = (f"{header}\n#include \"lvgl.h\"\n\n"
           f"/* RGB565A8: {w}x{h} RGB565 (LE) roi {w}x{h} alpha 8-bit */\n"
           f"static const uint8_t {name}_map[] = {{\n{c_bytes(rgb, 24)}\n    /* alpha */\n{c_bytes(alpha, 24)}\n}};\n\n"
           f"const lv_image_dsc_t {name} = {{\n    .header = {{\n        .magic = LV_IMAGE_HEADER_MAGIC,\n"
           f"        .cf = LV_COLOR_FORMAT_RGB565A8,\n        .w = {w},\n        .h = {h},\n        .stride = {w * 2},\n"
           f"    }},\n    .data_size = sizeof({name}_map),\n    .data = {name}_map,\n}};\n")
    open(os.path.join(out_dir, name + ".c"), "w", newline="\n").write(src)


def emit_a8(name, im, header, out_dir):
    w, h = im.size
    alpha = im.getchannel("A").tobytes()
    src = (f"{header}\n#include \"lvgl.h\"\n\n"
           f"static const uint8_t {name}_map[] = {{\n{c_bytes(alpha, w)}\n}};\n\n"
           f"const lv_image_dsc_t {name} = {{\n    .header = {{\n        .magic = LV_IMAGE_HEADER_MAGIC,\n"
           f"        .cf = LV_COLOR_FORMAT_A8,\n        .w = {w},\n        .h = {h},\n        .stride = {w},\n"
           f"    }},\n    .data_size = sizeof({name}_map),\n    .data = {name}_map,\n}};\n")
    open(os.path.join(out_dir, name + ".c"), "w", newline="\n").write(src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "components", "fd_ui"))
    ap.add_argument("--preview", default=None, help="PNG contact sheet cua 6 o Home (nen sang)")
    a = ap.parse_args()
    tiles = []
    for name, folder, fname, label, fix in TILES:
        url = FLUENT + urllib.parse.quote(f"{folder}/3D/{fname}")
        im = Image.open(io.BytesIO(fetch(url, fname))).convert("RGBA")
        if fix == "slate":
            im = recolor_slate(im)
        im = resize_premul(im, TILE_PX)
        tiles.append((label, im))
        hdr = (f"/* Icon o Home \"{label}\" - Microsoft Fluent Emoji 3D \"{folder}\", (c) Microsoft, MIT License.\n"
               f" * https://github.com/microsoft/fluentui-emoji  - {TILE_PX}x{TILE_PX} RGB565A8"
               + (", doi mau sang slate cho de nhin tren nen sang" if fix else "") +
               ".\n * Sinh tu dong boi tools/icons/gen_icons.py - khong sua tay. */")
        emit_rgb565a8(name, im, hdr, a.out)
    for name, sym, px, desc in SMALL:
        url = MSYM + f"{sym}/materialsymbolsrounded/{sym}_24px.svg"
        svg = fetch(url, f"{sym}_rounded_24px.svg")
        # render 4x roi Lanczos xuong (sac net hon render truc tiep o kich thuoc nho)
        png = cairosvg.svg2png(bytestring=svg, output_width=px * 4, output_height=px * 4)
        im = Image.open(io.BytesIO(png)).convert("RGBA")
        im = resize_premul(im, px)
        hdr = (f"/* Icon \"{desc}\" - Material Symbols (Rounded, 24px), (c) Google, Apache License 2.0.\n"
               f" * https://github.com/google/material-design-icons  - chuyen sang anh A8 {px}x{px} (chi kenh alpha),\n"
               f" * to mau bang style image_recolor (LVGL 9). Sinh tu dong boi tools/icons/gen_icons.py. */")
        emit_a8(name, im, hdr, a.out)
    if a.preview:
        S, PADX = 3, 8
        cw, ch = 96, 76
        sheet = Image.new("RGBA", (PADX + 3 * (cw + PADX), PADX + 2 * (ch + PADX)), (0xF3, 0xF4, 0xF6, 255))
        from PIL import ImageDraw, ImageFont
        d = ImageDraw.Draw(sheet)
        try:
            font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)   # co dau tieng Viet
        except OSError:
            font = ImageFont.load_default()
        for i, (label, im) in enumerate(tiles):
            x0 = PADX + (i % 3) * (cw + PADX)
            y0 = PADX + (i // 3) * (ch + PADX)
            d.rounded_rectangle((x0, y0, x0 + cw - 1, y0 + ch - 1), 12, fill="white", outline=(0xE5, 0xE7, 0xEB))
            sheet.alpha_composite(im, (x0 + (cw - TILE_PX) // 2, y0 + 6))
            d.text((x0 + cw // 2, y0 + 64), label, fill=(0x11, 0x18, 0x27), anchor="mm", font=font)
        sheet = sheet.resize((sheet.width * S, sheet.height * S), Image.NEAREST)
        sheet.convert("RGB").save(a.preview)


if __name__ == "__main__":
    main()
