#pragma once
/* Tu dien mini offline: mang da sap xep theo word (chu thuong), tra cuu bsearch.
 * Sau nay co the thay bang du lieu dong bo tu may chu. */
#include <stddef.h>

struct fd_fc_item {
    const char *word;      /* tieng Anh, chu thuong */
    const char *ipa;
    const char *meaning;   /* nghia tieng Viet ngan */
    const char *pos;       /* Optional part of speech from the synced word. */
};
