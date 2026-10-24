#include "fd_events.h"

ESP_EVENT_DEFINE_BASE(FD_EVENT);

esp_err_t fd_events_init(void)
{
    esp_err_t err = esp_event_loop_create_default();
    return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;
}

esp_err_t fd_events_post(int32_t id, const void *data, size_t len)
{
    return esp_event_post(FD_EVENT, id, data, len, 0);
}
