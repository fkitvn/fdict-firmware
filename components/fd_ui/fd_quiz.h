#pragma once
/* Trac nghiem: sinh bo cau hoi tu nguon tu vung (sau nay co the nhan bo cau hoi tu may chu). */
#include <stdbool.h>
#include <stdint.h>
#include "fd_dict_mini.h"

typedef enum { FD_QUIZ_SRC_FLASHCARD = 0, FD_QUIZ_SRC_DICT, FD_QUIZ_SRC_COUNT } fd_quiz_src_t;
typedef enum { FD_QUIZ_EN_VI = 0, FD_QUIZ_VI_EN } fd_quiz_dir_t;

#define FD_QUIZ_MAX 20

struct fd_quiz_q {
    const struct fd_fc_item *item;   /* tu goc (de hien IPA / phat am / danh sach sai) */
    const char *prompt;              /* tu tieng Anh (Anh-Viet) hoac nghia (Viet-Anh) */
    const char *choices[4];
    uint8_t correct_idx;
};

int fd_quiz_pool_size(fd_quiz_src_t src);
/* Sinh n cau (n <= pool, <= FD_QUIZ_MAX) voi tu khac nhau, tron ngau nhien. Tra so cau. */
int fd_quiz_generate(fd_quiz_src_t src, fd_quiz_dir_t dir, int n, struct fd_quiz_q *out);
/* Lam lai: giu cac tu, tron thu tu cau + dap an + tu nhieu moi. */
void fd_quiz_reshuffle(fd_quiz_src_t src, fd_quiz_dir_t dir, struct fd_quiz_q *qs, int n);