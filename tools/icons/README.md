# tools/icons — sinh icon C cho LVGL 9

`gen_icons.py` tải icon từ nguồn chính thức, rasterize và ghi file C vào `components/fd_ui/`.

```
pip install pillow cairosvg
python3 tools/icons/gen_icons.py                       # ghi vào components/fd_ui/
python3 tools/icons/gen_icons.py --preview preview.png # kèm ảnh xem trước 6 ô Home
```

File tải về được cache trong `tools/icons/cache/` (đã gitignore). Các file `img_*.c` sinh tự động, không sửa tay.

## Icon 6 ô Home — Microsoft Fluent Emoji 3D

Nguồn: https://github.com/microsoft/fluentui-emoji (`assets/<Tên>/3D/<tên>_3d.png`, 256×256) — © Microsoft, **MIT License**.
Thu nhỏ về 48×48 bằng Lanczos trên alpha nhân trước (tránh viền tối), xuất `LV_COLOR_FORMAT_RGB565A8`
(RGB565 little-endian + alpha 8-bit, 6912 B/icon; esp_lvgl_port tự swap byte khi đẩy ra ILI9341).

| Ô | Emoji Fluent | File C |
|---|---|---|
| Flash card | Card index 📇 | `img_tile_flashcard.c` |
| Học tập | Graduation cap 🎓 | `img_tile_study.c` |
| Từ điển | Blue book 📘 | `img_tile_dict.c` |
| Đồng bộ | Counterclockwise arrows button 🔄 | `img_tile_sync.c` |
| Thống kê | Bar chart 📊 | `img_tile_stats.c` |
| Cài đặt | Gear ⚙️ (đổi màu) | `img_tile_settings.c` |

Bánh răng Fluent gốc màu tím nhạt (lavender), khó nhìn trên nền sáng: script giữ độ sáng tối từng điểm
(khối 3D) nhưng chuyển sang tông xám xanh slate (hue #475569), kéo giãn tương phản, dải sáng 18–78% (tối hơn bản gốc).

## Icon nhỏ — Material Symbols Rounded

Nguồn: https://github.com/google/material-design-icons (`symbols/web/<tên>/materialsymbolsrounded/<tên>_24px.svg`)
— © Google, **Apache License 2.0**. Render SVG ở 4× rồi Lanczos xuống, xuất `LV_COLOR_FORMAT_A8`
(chỉ alpha, tô màu bằng `image_recolor`).

| Symbol | Cỡ | File C |
|---|---|---|
| screen_rotation | 24×24 | `img_rotate.c` |
| light_mode | 20×20 | `img_sun.c` |
| bedtime | 24×24 | `img_sleep.c` |

Các icon `LV_SYMBOL_*` (font Montserrat có sẵn của LVGL: WiFi, loa, pin, cài đặt trong panel…) giữ nguyên.
