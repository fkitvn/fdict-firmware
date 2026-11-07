/* Host test: build with a native C compiler; no ESP-IDF/RTOS dependency. */
#include "../../components/fd_net/wifi_retry.h"
int main(void)
{
    const unsigned expected[] = {0, 1500, 3000, 6000, 12000, 0};
    for (unsigned n = 0; n < sizeof(expected) / sizeof(expected[0]); ++n)
        if (wifi_api_retry_ms(n) != expected[n]) return 1;
    if (wifi_api_retry_ms(6) || wifi_api_retry_ms(~0u)) return 2;
    unsigned attempts = 0, total_wait = 0;
    do { ++attempts; total_wait += wifi_api_retry_ms(attempts); }
    while (wifi_api_retry_ms(attempts));
    if (attempts != WIFI_API_MAX_FAILURES || total_wait != 22500u) return 3;
    /* Manual retry starts a new budget; no inherited shift/overflow. */
    if (wifi_api_retry_ms(1) != 1500u) return 4;
    return 0;
}