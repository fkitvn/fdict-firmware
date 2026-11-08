#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Ap phim Telex `key` (ASCII) vao tu cuoi w[0..n) (codepoint). Tra do dai moi, -1 = khong bien doi (chen binh thuong). */
int fd_telex_feed(uint32_t *w, int n, int cap, char key);
int fd_telex_utf8_decode(const char *s, uint32_t *out, int cap);
int fd_telex_utf8_encode(const uint32_t *w, int n, char *out, int cap);