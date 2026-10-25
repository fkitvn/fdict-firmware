#pragma once
/* Event bus cua FDict One: dung default event loop cua ESP-IDF, base FD_EVENT. */
#include <stdbool.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(FD_EVENT);

typedef enum {
    FD_EVT_BUTTON = 1,          /* data: fd_evt_button_t */
    FD_EVT_WIFI_SCAN_DONE,      /* khong data; doc ket qua bang fd_net_scan_results() */
    FD_EVT_WIFI_CONNECTING,     /* khong data */
    FD_EVT_WIFI_CONNECTED,      /* khong data */
    FD_EVT_WIFI_DISCONNECTED,   /* data: fd_evt_wifi_disc_t */
    FD_EVT_TIME_SYNCED,         /* SNTP dat gio xong */
} fd_event_id_t;

typedef enum {
    FD_BTN_CLICK = 0,
    FD_BTN_DOUBLE_CLICK,
    FD_BTN_LONG_PRESS,
} fd_btn_action_t;

typedef struct {
    int index;              /* 0..3 = B1..B4 */
    fd_btn_action_t action;
} fd_evt_button_t;

typedef struct {
    int reason;             /* wifi_err_reason_t */
    bool auth_fail;         /* sai mat khau (khi ket noi mang moi) */
    bool auth_reject;       /* router khong xac thuc (reason 2/4/203/205), khong phai sai mat khau */
    bool no_ap;             /* khong thay mang */
    char ssid[33];
} fd_evt_wifi_disc_t;

/* Tao default event loop neu chua co. */
esp_err_t fd_events_init(void);
esp_err_t fd_events_post(int32_t id, const void *data, size_t len);

#ifdef __cplusplus
}
#endif
