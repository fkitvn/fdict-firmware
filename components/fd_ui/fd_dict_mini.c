/* Tu dien mini offline (~60 tu thong dung). PHAI giu thu tu tang dan theo word (strcmp) cho bsearch. */
#include "fd_dict_mini.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const struct fd_fc_item DICT[] = {
    {"answer", "/ˈɑːn.sər/", "câu trả lời; trả lời", NULL},
    {"apple", "/ˈæp.əl/", "quả táo", NULL},
    {"beautiful", "/ˈbjuː.tɪ.fəl/", "đẹp", NULL},
    {"book", "/bʊk/", "quyển sách; đặt trước", NULL},
    {"bread", "/bred/", "bánh mì", NULL},
    {"brother", "/ˈbrʌð.ər/", "anh trai, em trai", NULL},
    {"buy", "/baɪ/", "mua", NULL},
    {"car", "/kɑːr/", "ô tô", NULL},
    {"cat", "/kæt/", "con mèo", NULL},
    {"city", "/ˈsɪt.i/", "thành phố", NULL},
    {"computer", "/kəmˈpjuː.tər/", "máy tính", NULL},
    {"curious", "/ˈkjʊə.ri.əs/", "tò mò", NULL},
    {"day", "/deɪ/", "ngày", NULL},
    {"dog", "/dɒɡ/", "con chó", NULL},
    {"door", "/dɔːr/", "cửa ra vào", NULL},
    {"drink", "/drɪŋk/", "uống; đồ uống", NULL},
    {"easy", "/ˈiː.zi/", "dễ dàng", NULL},
    {"eat", "/iːt/", "ăn", NULL},
    {"family", "/ˈfæm.əl.i/", "gia đình", NULL},
    {"father", "/ˈfɑː.ðər/", "bố, cha", NULL},
    {"friend", "/frend/", "bạn bè", NULL},
    {"go", "/ɡəʊ/", "đi", NULL},
    {"good", "/ɡʊd/", "tốt, giỏi", NULL},
    {"happy", "/ˈhæp.i/", "vui vẻ, hạnh phúc", NULL},
    {"help", "/help/", "giúp đỡ", NULL},
    {"house", "/haʊs/", "ngôi nhà", NULL},
    {"improve", "/ɪmˈpruːv/", "cải thiện", NULL},
    {"journey", "/ˈdʒɜː.ni/", "chuyến đi, hành trình", NULL},
    {"knowledge", "/ˈnɒl.ɪdʒ/", "kiến thức", NULL},
    {"language", "/ˈlæŋ.ɡwɪdʒ/", "ngôn ngữ", NULL},
    {"learn", "/lɜːn/", "học", NULL},
    {"love", "/lʌv/", "yêu; tình yêu", NULL},
    {"measure", "/ˈmeʒ.ər/", "đo lường", NULL},
    {"money", "/ˈmʌn.i/", "tiền", NULL},
    {"morning", "/ˈmɔː.nɪŋ/", "buổi sáng", NULL},
    {"mother", "/ˈmʌð.ər/", "mẹ", NULL},
    {"music", "/ˈmjuː.zɪk/", "âm nhạc", NULL},
    {"name", "/neɪm/", "tên", NULL},
    {"night", "/naɪt/", "ban đêm", NULL},
    {"open", "/ˈəʊ.pən/", "mở; đang mở", NULL},
    {"people", "/ˈpiː.pəl/", "mọi người, con người", NULL},
    {"phone", "/fəʊn/", "điện thoại", NULL},
    {"question", "/ˈkwes.tʃən/", "câu hỏi", NULL},
    {"read", "/riːd/", "đọc", NULL},
    {"rice", "/raɪs/", "gạo, cơm", NULL},
    {"run", "/rʌn/", "chạy", NULL},
    {"school", "/skuːl/", "trường học", NULL},
    {"sister", "/ˈsɪs.tər/", "chị gái, em gái", NULL},
    {"sleep", "/sliːp/", "ngủ; giấc ngủ", NULL},
    {"speak", "/spiːk/", "nói", NULL},
    {"student", "/ˈstjuː.dənt/", "học sinh, sinh viên", NULL},
    {"sun", "/sʌn/", "mặt trời", NULL},
    {"teacher", "/ˈtiː.tʃər/", "giáo viên", NULL},
    {"thank", "/θæŋk/", "cảm ơn", NULL},
    {"thought", "/θɔːt/", "suy nghĩ, ý nghĩ", NULL},
    {"time", "/taɪm/", "thời gian; lần", NULL},
    {"today", "/təˈdeɪ/", "hôm nay", NULL},
    {"tomorrow", "/təˈmɒr.əʊ/", "ngày mai", NULL},
    {"water", "/ˈwɔː.tər/", "nước", NULL},
    {"weather", "/ˈweð.ər/", "thời tiết", NULL},
    {"word", "/wɜːd/", "từ, lời nói", NULL},
    {"work", "/wɜːk/", "làm việc; công việc", NULL},
    {"write", "/raɪt/", "viết", NULL},
    {"year", "/jɪər/", "năm", NULL},
    {"yesterday", "/ˈjes.tə.deɪ/", "hôm qua", NULL},
};
#define N_DICT (sizeof(DICT) / sizeof(DICT[0]))

static int cmp(const void *key, const void *el)
{
    return strcmp((const char *)key, ((const struct fd_fc_item *)el)->word);
}

const struct fd_fc_item *fd_dict_lookup(const char *word)
{
    char k[40];
    size_t n = 0;
    if (!word) return NULL;
    while (*word && isspace((unsigned char)*word)) word++;
    while (*word && n < sizeof(k) - 1) k[n++] = (char)tolower((unsigned char)*word++);
    while (n && isspace((unsigned char)k[n - 1])) n--;
    k[n] = 0;
    if (!n) return NULL;
    return bsearch(k, DICT, N_DICT, sizeof(DICT[0]), cmp);
}

size_t fd_dict_count(void) { return N_DICT; }
const struct fd_fc_item *fd_dict_at(size_t i) { return i < N_DICT ? &DICT[i] : NULL; }