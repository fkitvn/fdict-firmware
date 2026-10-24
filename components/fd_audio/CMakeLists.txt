idf_component_register(SRCS "fd_audio.c" "fd_es8311.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "."
                    REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_gpio board_fnk0104b nvs_flash esp_timer fd_tts)