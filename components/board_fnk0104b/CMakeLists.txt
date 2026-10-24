idf_component_register(SRCS "board.c" "board_lcd.c" "board_touch.c" "board_buttons.c" "board_rgb.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_ledc esp_driver_rmt esp_lcd
                             espressif__esp_lcd_ili9341 espressif__esp_lcd_touch_ft5x06 espressif__button)
