/* Sinh cau hoi trac nghiem 4 lua chon: 1 dung + 3 tu nhieu khac nhau cung nguon, tron bang esp_random. */
#include "fd_quiz.h"
#include <string.h>
#include "esp_random.h"

/* Nguon "Flash card": 10 tu cua the (deu co trong tu dien mini) */
static const char *const FC_WORDS[] = {"apple",   "beautiful", "journey", "knowledge", "thought",
                                       "weather", "measure",   "curious", "improve",   "language"};
#define N_FC ((int)(sizeof(FC_WORDS) / sizeof(FC_WORDS[0])))

static const struct fd_fc_item *pool_at(fd_quiz_src_t src, int i)
{
    return src == FD_QUIZ_SRC_FLASHCARD ? fd_dict_lookup(FC_WORDS[i]) : fd_dict_at((size_t)i);
}

int fd_quiz_pool_size(fd_quiz_src_t src) { return src == FD_QUIZ_SRC_FLASHCARD ? N_FC : (int)fd_dict_count(); }

static int rnd(int n) { return (int)(esp_random() % (uint32_t)n); }

static const char *ans(const struct fd_fc_item *it, fd_quiz_dir_t dir) { return dir == FD_QUIZ_EN_VI ? it->meaning : it->word; }

static void build_q(fd_quiz_src_t src, fd_quiz_dir_t dir, const struct fd_fc_item *it, struct fd_quiz_q *q)
{
    int np = fd_quiz_pool_size(src);
    q->item = it;
    q->prompt = dir == FD_QUIZ_EN_VI ? it->word : it->meaning;
    const char *c[4] = {ans(it, dir)};
    int k = 1;
    for (int guard = 0; k < 4 && guard < 200; guard++) {
        const struct fd_fc_item *d = pool_at(src, rnd(np));
        if (!d) continue;
        const char *a = ans(d, dir);
        bool dup = false;
        for (int j = 0; j < k; j++) dup |= strcmp(c[j], a) == 0;
        if (!dup) c[k++] = a;
    }
    while (k < 4) { c[k] = "?"; k++; }   /* khong xay ra voi nguon >= 4 tu */
    int pos = rnd(4);
    for (int i = 0, j = 1; i < 4; i++) q->choices[i] = i == pos ? c[0] : c[j++];
    q->correct_idx = (uint8_t)pos;
}

int fd_quiz_generate(fd_quiz_src_t src, fd_quiz_dir_t dir, int n, struct fd_quiz_q *out)
{
    int np = fd_quiz_pool_size(src);
    uint8_t idx[100];
    if (np > (int)sizeof(idx)) np = sizeof(idx);
    if (n > np) n = np;
    if (n > FD_QUIZ_MAX) n = FD_QUIZ_MAX;
    for (int i = 0; i < np; i++) idx[i] = (uint8_t)i;
    for (int i = 0; i < n; i++) {   /* Fisher-Yates mot phan */
        int j = i + rnd(np - i);
        uint8_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
        build_q(src, dir, pool_at(src, idx[i]), &out[i]);
    }
    return n;
}

void fd_quiz_reshuffle(fd_quiz_src_t src, fd_quiz_dir_t dir, struct fd_quiz_q *qs, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rnd(i + 1);
        const struct fd_fc_item *t = qs[i].item; qs[i].item = qs[j].item; qs[j].item = t;
    }
    for (int i = 0; i < n; i++) build_q(src, dir, qs[i].item, &qs[i]);
}