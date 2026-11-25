idf_component_register(SRCS "fd_berry.c"
                    INCLUDE_DIRS "include"
                    REQUIRES berry esp_timer heap esp_system freertos nvs_flash)
