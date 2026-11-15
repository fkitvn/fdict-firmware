"""Parse LVGL fmt_txt font .c files: codepoints and .fallback."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def font_c_paths():
    paths = []
    for folder in (ROOT / "components/fd_fonts", ROOT / "components/fd_ui"):
        if folder.exists():
            paths.extend(sorted(folder.glob("fd_font_*.c")))
    return paths


def font_path(name):
    for p in font_c_paths():
        if p.stem == name:
            return p
    raise FileNotFoundError(name)


def _nums(ident, src):
    m = re.search(r"\b" + ident + r"\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        return []
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(x, 0) for x in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", body)]


def font_codepoints(src_or_name):
    """Glyphs contained in this font file itself (no fallback)."""
    if isinstance(src_or_name, Path) or str(src_or_name).endswith(".c"):
        s = Path(src_or_name).read_text(encoding="utf-8")
    elif "\n" in str(src_or_name):
        s = src_or_name
    else:
        s = font_path(src_or_name).read_text(encoding="utf-8")
    glyphs = set()
    parts = s.split("cmaps[]", 1)
    if len(parts) < 2:
        return glyphs
    cmap = parts[1].split("/*-----------------", 1)[0]
    for start, length, gid, ul in re.findall(
        r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*\.unicode_list = (\w+)",
        cmap,
    ):
        offsets = range(int(length)) if ul == "NULL" else _nums(ul, s)
        for v in offsets:
            glyphs.add(int(start) + v)
    return glyphs


def font_fallback_name(src_or_name):
    """Return fallback identifier ('fd_font_noto_18') or None."""
    if isinstance(src_or_name, Path) or str(src_or_name).endswith(".c"):
        s = Path(src_or_name).read_text(encoding="utf-8")
    elif "\n" in str(src_or_name) and not str(src_or_name).endswith(".c"):
        s = src_or_name
    else:
        s = font_path(src_or_name).read_text(encoding="utf-8")
    # Public lv_font_t descriptor (the last .fallback in the file is also fine).
    matches = list(re.finditer(r"\.fallback\s*=\s*([^,]+)", s))
    if not matches:
        return None
    v = matches[-1].group(1).strip()
    if v in ("NULL", "0", "(void *)0", "((void *)0)"):
        return None
    return v.lstrip("&").strip()


def sample_cps(path):
    cps = set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        t = line.split("\t", 1)[-1]
        for ch in t:
            o = ord(ch)
            if o >= 0x20:
                cps.add(o)
    return cps


def unescape_c(s):
    out = []
    i = 0
    while i < len(s):
        if s[i] != "\\" or i + 1 >= len(s):
            out.append(s[i])
            i += 1
            continue
        n = s[i + 1]
        mapping = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'"}
        out.append(mapping.get(n, n))
        i += 2
    return "".join(out)


def c_phrases():
    s = text("components/fd_i18n/lv_i18n.c")
    rows = []
    for name in ("en_singulars", "vi_singulars"):
        m = re.search(r"static const lv_i18n_phrase_t " + name + r"\[\] = \{(.*?)\n\};", s, re.S)
        if not m:
            continue
        for k, t in re.findall(r'^\s*\{"([A-Z0-9_]+)", "((?:[^"\\]|\\.)*)"\}', m.group(1), re.M):
            rows.append((name[:2], k, unescape_c(t)))
    return rows


def instantiate(s):
    import re
    out = []
    i = 0
    n = 0
    dummy = ["3", "5", "10", "42"]
    while i < len(s):
        if s[i] == "%" and i + 1 < len(s):
            if s[i + 1] == "%":
                out.append("%")
                i += 2
                continue
            m = re.match(r"%[-+ #0]*\d*(?:\.\d+)?([diuxXcs])", s[i:])
            if m:
                spec = m.group(1)
                if spec in "diuxX":
                    out.append(dummy[n % len(dummy)])
                    n += 1
                elif spec == "c":
                    out.append("A")
                else:
                    out.append("Ab")
                i += m.end()
                continue
        out.append(s[i])
        i += 1
    return "".join(out)
