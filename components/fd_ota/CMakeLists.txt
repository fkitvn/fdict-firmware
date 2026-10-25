idf_component_register(SRCS "fd_ota.c" "fd_study.c" "fd_deck.c" "fd_flashcard.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_https_ota esp_http_client esp_partition app_update esp_app_format esp_system esp_timer
                             esp_hw_support nvs_flash mbedtls fd_events fd_net fd_power)
