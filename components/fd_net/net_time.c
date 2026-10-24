/* SNTP + mui gio. TZ POSIX "ICT-7" = Asia/Bangkok (UTC+7, khong DST). */
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "fd_events.h"
#include "fd_net.h"
#include "sdkconfig.h"

static const char *TAG = "fd_time";
static bool s_started;

static void sync_cb(struct timeval *tv)
{
    (void)tv;
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
    ESP_LOGI(TAG, "SNTP dong bo xong: %s (%s)", buf, CONFIG_FD_TZ);
    fd_events_post(FD_EVT_TIME_SYNCED, NULL, 0);
}

void fd_net_time_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;
    setenv("TZ", CONFIG_FD_TZ, 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_FD_NTP_SERVER);
    cfg.sync_cb = sync_cb;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    ESP_LOGI(TAG, "SNTP %s: %s", CONFIG_FD_NTP_SERVER, esp_err_to_name(err));
}

bool fd_net_time_valid(void)
{
    return time(NULL) > 1767225600;   /* > 2026-01-01 */
}

void fd_net_iso_utc(char *out, size_t len)
{
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &t);
}
