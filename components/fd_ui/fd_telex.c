/* fd_telex: go tieng Viet kieu Telex cho o nhap Tu dien (che do Viet - Anh).
 * aa/ee/oo -> a^ e^ o^, aw/ow/uw/w -> a( o+ u+, dd -> d-, s f r x j = 5 dau, z = bo dau.
 * Go lap phim lan nua de hoan tac (vd "aaa" -> "aa", "ass" -> "as"). */
#include "fd_telex.h"
#include <string.h>

static const char *const k_rows[24] = {
    "aáàảãạ", "ăắằẳẵặ", "âấầẩẫậ", "eéèẻẽẹ", "êếềểễệ", "iíìỉĩị",
    "oóòỏõọ", "ôốồổỗộ", "ơớờởỡợ", "uúùủũụ", "ưứừửữự", "yýỳỷỹỵ",
    "AÁÀẢÃẠ", "ĂẮẰẲẴẶ", "ÂẤẦẨẪẬ", "EÉÈẺẼẸ", "ÊẾỀỂỄỆ", "IÍÌỈĨỊ",
    "OÓÒỎÕỌ", "ÔỐỒỔỖỘ", "ƠỚỜỞỠỢ", "UÚÙỦŨỤ", "ƯỨỪỬỮỰ", "YÝỲỶỸỴ",
};
enum { B_A, B_AW, B_AA, B_E, B_EE, B_I, B_O, B_OO, B_OW, B_U, B_UW, B_Y, B_N };
static uint32_t s_tab[24][6];
static bool s_init;

int fd_telex_utf8_decode(const char *s, uint32_t *out, int cap)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < cap) {
        uint32_t c = *p++;
        int extra = 0;
        if (c >= 0xF0) { c &= 0x07; extra = 3; }
        else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
        else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
        while (extra-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
        out[n++] = c;
    }
    return n;
}

int fd_telex_utf8_encode(const uint32_t *w, int n, char *out, int cap)
{
    int o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t c = w[i];
        char b[4]; int k;
        if (c < 0x80) { b[0] = (char)c; k = 1; }
        else if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3F)); k = 2; }
        else { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); k = 3; }
        if (o + k >= cap) break;
        memcpy(out + o, b, (size_t)k);
        o += k;
    }
    out[o] = 0;
    return o;
}

static void init_tab(void)
{
    if (s_init) return;
    for (int r = 0; r < 24; r++) fd_telex_utf8_decode(k_rows[r], s_tab[r], 6);
    s_init = true;
}

/* vowel -> base (0..11), tone (0..5), upper; false neu khong phai nguyen am */
static bool split(uint32_t c, int *base, int *tone, bool *up)
{
    for (int r = 0; r < 24; r++)
        for (int t = 0; t < 6; t++)
            if (s_tab[r][t] == c) { *base = r % 12; *tone = t; *up = r >= 12; return true; }
    return false;
}
static uint32_t join(int base, int tone, bool up) { return s_tab[base + (up ? 12 : 0)][tone]; }

static int tone_of_key(char k)
{
    switch (k) { case 's': return 1; case 'f': return 2; case 'r': return 3; case 'x': return 4; case 'j': return 5; case 'z': return 0; }
    return -1;
}

/* vi tri nguyen am nhan dau trong tu */
static int tone_pos(const uint32_t *w, int n)
{
    int b, t; bool u;
    int end = -1, start = -1;
    for (int i = n - 1; i >= 0; i--) {
        if (split(w[i], &b, &t, &u)) { if (end < 0) end = i; start = i; }
        else if (end >= 0) break;
    }
    if (end < 0) return -1;
    /* qu / gi: u, i la phu am khi con nguyen am khac */
    if (end > start && start > 0) {
        uint32_t p = w[start - 1] | 0x20, c0 = w[start];
        split(c0, &b, &t, &u);
        if ((p == 'q' && b == B_U) || (p == 'g' && b == B_I)) start++;
    }
    int len = end - start + 1;
    for (int i = end; i >= start; i--) {   /* uu tien nguyen am co mu/rau (uo+ -> o+) */
        split(w[i], &b, &t, &u);
        if (b == B_AW || b == B_AA || b == B_EE || b == B_OO || b == B_OW || b == B_UW) return i;
    }
    if (len == 1) return start;
    if (end < n - 1) return end;           /* co phu am cuoi: toan, tien */
    if (len == 3) return start + 1;        /* oai, uoi */
    int b0, b1;
    split(w[start], &b0, &t, &u); split(w[start + 1], &b1, &t, &u);
    if ((b0 == B_O && (b1 == B_A || b1 == B_E)) || (b0 == B_U && b1 == B_Y)) return start + 1;   /* hoa, khoe, thuy */
    return start;                          /* mia, mua, hai */
}

int fd_telex_feed(uint32_t *w, int n, int cap, char key)
{
    init_tab();
    if (n <= 0) {
        if ((key | 0x20) == 'w' && cap > 0) { w[0] = key == 'W' ? 0x01AF : 0x01B0; return 1; }
        return -1;
    }
    bool kup = key >= 'A' && key <= 'Z';
    char k = (char)(key | 0x20);
    int b, t; bool u;

    if (k == 'd') {
        uint32_t last = w[n - 1];
        if (last == 'd' || last == 'D') { w[n - 1] = last == 'D' ? 0x0110 : 0x0111; return n; }
        if (last == 0x0111 || last == 0x0110) {
            if (n + 1 > cap) return -1;
            w[n - 1] = last == 0x0110 ? 'D' : 'd'; w[n++] = kup ? 'D' : 'd'; return n;
        }
        return -1;
    }

    int tone = tone_of_key(k);
    if (tone >= 0) {
        int p = tone_pos(w, n);
        if (p < 0) return -1;
        split(w[p], &b, &t, &u);
        if (tone == 0 && t == 0) return -1;
        if (t == tone && tone != 0) {         /* hoan tac */
            if (n + 1 > cap) return -1;
            w[p] = join(b, 0, u); w[n++] = (uint32_t)key; return n;
        }
        w[p] = join(b, tone, u);
        return n;
    }

    if (k == 'a' || k == 'e' || k == 'o') {
        int plain = k == 'a' ? B_A : k == 'e' ? B_E : B_O;
        int hat = k == 'a' ? B_AA : k == 'e' ? B_EE : B_OO;
        int alt = k == 'a' ? B_AW : k == 'o' ? B_OW : -1;
        for (int i = n - 1; i >= 0 && i >= n - 3; i--) {
            if (!split(w[i], &b, &t, &u)) { if (i < n - 1) break; continue; }
            if (b == plain || b == alt) { w[i] = join(hat, t, u); return n; }
            if (b == hat) {
                if (n + 1 > cap) return -1;
                w[i] = join(plain, t, u); w[n++] = (uint32_t)key; return n;
            }
        }
        return -1;
    }

    if (k == 'w') {
        /* uo -> u+o+ */
        for (int i = n - 1; i > 0; i--) {
            int b0, t0, b1, t1; bool u0, u1;
            if (split(w[i - 1], &b0, &t0, &u0) && split(w[i], &b1, &t1, &u1) && b0 == B_U && (b1 == B_O || b1 == B_OO)) {
                w[i - 1] = join(B_UW, t0, u0); w[i] = join(B_OW, t1, u1); return n;
            }
        }
        for (int i = n - 1; i >= 0; i--) {
            if (!split(w[i], &b, &t, &u)) continue;
            int to = b == B_A || b == B_AA ? B_AW : b == B_O || b == B_OO ? B_OW : b == B_U ? B_UW : -1;
            if (to >= 0) { w[i] = join(to, t, u); return n; }
            int back = b == B_AW ? B_A : b == B_OW ? B_O : b == B_UW ? B_U : -1;
            if (back >= 0) {
                if (n + 1 > cap) return -1;
                w[i] = join(back, t, u); w[n++] = (uint32_t)key; return n;
            }
            break;
        }
        return -1;
    }
    return -1;
}