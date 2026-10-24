idf_component_register(SRCS "fd_voice.c"
                    INCLUDE_DIRS "include"
                    REQUIRES fd_audio fd_ota fd_net esp_timer)