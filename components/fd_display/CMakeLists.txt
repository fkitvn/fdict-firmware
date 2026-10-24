idf_component_register(SRCS "fd_display.c"
                    INCLUDE_DIRS "include"
                    REQUIRES board_fnk0104b espressif__esp_lvgl_port lvgl__lvgl esp_lcd nvs_flash)
