# Third-party notices

| Thành phần | Vị trí | Nguồn | Giấy phép |
|---|---|---|---|
| SVOX Pico TTS (engine + en-US lingware `en-US_ta.bin`, `en-US_lh0_sg.bin`) | `components/fd_tts/pico/` | https://github.com/naggety/picotts (từ AOSP `external/svox`), © 2008-2009 SVOX AG | Apache License 2.0 – `components/fd_tts/pico/lib/NOTICE`; thay đổi của FDict One: `components/fd_tts/NOTICE-PICO.txt` |
| Ooura FFT (trong `picofftsg.c`) | `components/fd_tts/pico/lib/` | http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html, © Takuya Ooura | Giấy phép tự do của tác giả (cho phép dùng/sửa/phân phối, giữ ghi chú) |
| Noto Sans (font LVGL sinh ra) | `assets/fonts/NotoSans-Regular.ttf`, `components/fd_fonts/` | https://github.com/notofonts | SIL Open Font License 1.1 – `assets/fonts/OFL.txt` |
| Montserrat (font tích hợp LVGL, nếu bật trong sdkconfig) | LVGL | https://github.com/JulietaUla/Montserrat | SIL Open Font License 1.1 |
| Fluent Emoji (ô menu `img_tile_*.c`) | `components/fd_ui/` | https://github.com/microsoft/fluentui-emoji | MIT License, © Microsoft Corporation |
| Material Symbols (`img_rotate.c`, `img_sleep.c`, `img_sun.c`) | `components/fd_ui/` | https://github.com/google/material-design-icons | Apache License 2.0 |
| Noto Emoji (`img_ok_hand.c`) | `components/fd_ui/` | https://github.com/googlefonts/noto-emoji | Apache License 2.0 (ảnh: SIL OFL 1.1 cho font) |
| LVGL | Component Manager `lvgl/lvgl` | https://github.com/lvgl/lvgl | MIT |
| ESP-IDF | toolchain/SDK | https://github.com/espressif/esp-idf | Apache License 2.0 |
| esp_lvgl_port, esp_lcd_ili9341, esp_lcd_touch(_ft5x06), button, cmake_utilities | Component Manager `espressif/*` | https://components.espressif.com | Apache License 2.0 |
| cJSON | Component Manager `espressif/cjson` | https://github.com/DaveGamble/cJSON | MIT |
| esp_littlefs / littlefs | Component Manager `joltwallet/littlefs` | https://github.com/joltwallet/esp_littlefs | MIT (esp_littlefs), BSD-3-Clause (littlefs) |
| lv_font_conv (công cụ, không nằm trong firmware) | `tools/gen_fonts.sh` | https://github.com/lvgl/lv_font_conv | MIT |

Khi phân phối bản nhị phân có Pico TTS: kèm theo bản Apache-2.0 và file NOTICE của Pico.
**Thương hiệu:** tên FDict (thiết bị: FDict One) / fkit.vn và logo (`assets/images/fdict-logo.svg`, ảnh dẫn xuất như `components/fd_ui/img_logo.c`) thuộc sở hữu của fkit.vn, không cấp phép theo Apache-2.0.
