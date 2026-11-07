#pragma once
/* Immediate local driver/API failures, separate from AP/authentication retries.
 * Five attempts total: wait 1.5, 3, 6 and 12 seconds, then explicit error. */
#define WIFI_API_MAX_FAILURES 5u
static inline unsigned wifi_api_retry_ms(unsigned failures)
{
    if (failures == 0 || failures >= WIFI_API_MAX_FAILURES) return 0;
    return 1500u << (failures - 1u);
}
