#pragma once
/*
 * So do chan Freenove ESP32-S3 Display 2.8" (FNK0104B, co cam ung).
 * Nguon: sketches cu, TFT_eSPI_Setups/FNK0104AB_2.8_240x320_ILI9341.h (Freenove),
 *        xiaozhi-esp32 main/boards/freenove-esp32s3-display-2.8-lcd/config.h (MIT).
 */
#include "driver/gpio.h"

/* LCD ILI9341 qua SPI */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_MOSI          GPIO_NUM_11
#define BOARD_LCD_MISO          GPIO_NUM_13
#define BOARD_LCD_SCLK          GPIO_NUM_12
#define BOARD_LCD_CS            GPIO_NUM_10
#define BOARD_LCD_DC            GPIO_NUM_46   /* strapping pin, chi dung lam output sau boot */
#define BOARD_LCD_RST           GPIO_NUM_NC   /* noi chung reset he thong */
#define BOARD_LCD_BL            GPIO_NUM_45   /* strapping pin (VDD_SPI), active-high */
#define BOARD_LCD_H_RES         240
#define BOARD_LCD_V_RES         320

/* I2C chung: touch FT6336U (0x38) + codec ES8311 (0x18) */
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_SCL           GPIO_NUM_15
#define BOARD_I2C_SDA           GPIO_NUM_16
#define BOARD_TOUCH_RST         GPIO_NUM_18
#define BOARD_TOUCH_INT         GPIO_NUM_17

/* Audio I2S + ES8311 */
#define BOARD_I2S_MCLK          GPIO_NUM_4
#define BOARD_I2S_BCLK          GPIO_NUM_5
#define BOARD_I2S_DIN           GPIO_NUM_6
#define BOARD_I2S_WS            GPIO_NUM_7
#define BOARD_I2S_DOUT          GPIO_NUM_8
#define BOARD_AMP_EN            GPIO_NUM_1
#define BOARD_AMP_ON_LEVEL      0             /* active-low (20/10: thu active-high van khong co tieng -> tra ve 0) */

/* microSD SDMMC 4-bit */
#define BOARD_SD_CMD            GPIO_NUM_40
#define BOARD_SD_CLK            GPIO_NUM_38
#define BOARD_SD_D0             GPIO_NUM_39
#define BOARD_SD_D1             GPIO_NUM_41
#define BOARD_SD_D2             GPIO_NUM_48
#define BOARD_SD_D3             GPIO_NUM_47

/* WS2812 */
#define BOARD_RGB_LED           GPIO_NUM_42

/* 4 nut, active-low */
#define BOARD_BTN_COUNT         4
#define BOARD_BTN1              GPIO_NUM_21
#define BOARD_BTN2              GPIO_NUM_2
#define BOARD_BTN3              GPIO_NUM_3    /* strapping pin */
#define BOARD_BTN4              GPIO_NUM_14
