#pragma once
/* Tu dien mini offline: mang da sap xep theo word (chu thuong), tra cuu bsearch.
 * Sau nay co the thay bang du lieu dong bo tu may chu. */
#include <stddef.h>

struct fd_fc_item {
    const char *word;      /* tieng Anh, chu thuong */
    const char *ipa;
    const char *meaning;   /* nghia tieng Viet ngan */
    const char *audio;     /* TODO: duong dan / id am thanh phat am that (NULL = chua co, phat chuong mockup) */
};

/* Tra cuu khong phan biet hoa thuong, bo khoang trang dau/cuoi. NULL neu khong co. */
const struct fd_fc_item *fd_dict_lookup(const char *word);
size_t fd_dict_count(void);
const struct fd_fc_item *fd_dict_at(size_t i);