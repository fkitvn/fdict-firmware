idf_component_register(SRCS "wifi_mgr.c" "net_time.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_phy esp_wifi esp_netif esp_event nvs_flash esp_timer lwip fd_events)
