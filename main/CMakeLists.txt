idf_component_register(SRCS "app_main.c" "sys_info.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash efuse esp_system esp_psram esp_partition app_update spi_flash heap
                             board_fnk0104b fd_events fd_display fd_store fd_net fd_ui fd_power fd_ota)
