"""Run after lv_i18n compile; keeps generated tables in flash without const casts."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
for name in ("lv_i18n.c", "lv_i18n.h"):
    p = root / "components/fd_i18n" / name
    s = p.read_text(encoding="utf-8")
    s = s.replace("static lv_i18n_phrase_t ", "static const lv_i18n_phrase_t ")
    s = s.replace("__lv_i18n_get_text_core(lv_i18n_phrase_t *", "__lv_i18n_get_text_core(const lv_i18n_phrase_t *")
    s = s.replace("    lv_i18n_phrase_t * singulars;", "    const lv_i18n_phrase_t * singulars;")
    s = s.replace("    lv_i18n_phrase_t * plurals[", "    const lv_i18n_phrase_t * plurals[")
    p.write_text(s, encoding="utf-8", newline="\n")
