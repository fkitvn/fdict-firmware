## 4.0.0 — phân vùng mới (bản phát triển)

- Flash 16 MiB: `ota_0` tại `0x20000`, `ota_1` tại `0x320000`, mỗi slot `0x300000` (3 MiB).
- LittleFS (`storage`) tại `0x620000`, dài `0x810000` (8,0625 MiB, màn hình làm tròn thành 8,1 MB). Voice vẫn tại `0xE30000`.
- **Cần USB full.bin tại 0x0 để áp dụng bảng phân vùng mới.** Sao lưu nếu cần, chấp nhận xóa dữ liệu cũ; sau khi nạp, xác nhận **Khôi phục cài đặt gốc** trên thiết bị để khởi tạo LittleFS. Firmware không tự format khi mount lỗi.
- OTA chỉ thay app, không đổi bảng phân vùng hoặc tăng LittleFS. Không có cơ chế chuyển đổi/gate thiết bị dùng bảng cũ trong bản phát triển này. Không tự flash hay xóa thiết bị.
- `Cài đặt > Thông tin phiên bản > LittleFS` hiển thị dung lượng đang dùng / tổng thực tế của phân vùng được mount.

# FDict One – Máy học từ vựng

Firmware cho **FDict One**, máy học từ vựng cầm tay (Anh–Việt hoặc Pháp–Việt) chạy trên **ESP32-S3** (bo mạch **Freenove FNK0104B**).
Viết bằng C trên ESP-IDF 6.1 và LVGL 9.

_English summary at the end._

## Mục lục

- [Tính năng](#tính-năng)
- [Phần cứng](#phần-cứng)
- [Bảng phân vùng](#bảng-phân-vùng)
- [Yêu cầu build](#yêu-cầu-build)
- [Cấu hình](#cấu-hình)
- [Build và nạp](#build-và-nạp)
- [app.bin và full.bin](#appbin-và-fullbin)
- [Cập nhật OTA](#cập-nhật-ota)
- [Phát hành OTA qua GitHub Actions](#phát-hành-ota-qua-github-actions)
- [Cấu trúc dự án](#cấu-trúc-dự-án)
- [Đóng góp](#đóng-góp)
- [Giấy phép](#giấy-phép)

## Tính năng

- Giao diện LVGL 9, hỗ trợ màn hình dọc 240x320 và ngang 320x240 (xoay trong Cài đặt), font Noto Sans tiếng Việt.
- **Flash card**: màn cài đặt trước phiên (số thẻ, thứ tự, mặt trước, tự động phát âm), thẻ hai mặt chạm để lật.
- **Luyện tập**: trắc nghiệm 4 đáp án dạng lưới 2x2 (Anh–Việt / Việt–Anh), số câu 10 / 20 / Vô tận, tự phát âm Tắt / 1 lần / 2 lần.
- **Từ điển**: tra hai chiều Anh–Việt, gõ tiếng Việt.
- **Sắp xếp câu**, **Phân loại từ**, **Điền từ**.
- **Học offline**: bài học tải từ máy chủ được lưu cache trên LittleFS; kết quả lưu vào hàng chờ và tự gửi khi có mạng.
- **Phát âm offline** bằng SVOX Pico TTS. Phân vùng `voice` chứa một giọng: Anh (`en-US`) hoặc Pháp (`fr-FR`). Cùng một app, hai file nạp USB. OTA không đổi giọng. Khi có mạng có thể tải âm thanh từ máy chủ về cache.
- Wi-Fi: quét mạng, lưu nhiều mạng, nhập mật khẩu trên màn hình.
- Ghép thiết bị với tài khoản web bằng mã QR.
- Cập nhật firmware OTA qua HTTPS, kiểm tra SHA256 và tự rollback nếu bản mới lỗi.
- Cài đặt: độ sáng, âm lượng, thời gian tắt màn hình, mã PIN, hướng xoay, thông tin phiên bản; nút Ngủ (deep sleep) trong bảng cài đặt nhanh; hiển thị % pin.

## Phần cứng

- Freenove **FNK0104B**: ESP32-S3, flash 16 MB, PSRAM 8MB.
- LCD ILI9341 (SPI) + cảm ứng FT6336U (driver FT5x06).
- Codec âm thanh ES8311 (I2S) + mạch khuếch đại loa.
- 4 nút bấm, LED RGB WS2812, đo pin qua ADC.
- Sơ đồ chân: `components/board_fnk0104b/include/board_pins.h`.

## Bảng phân vùng

Flash 16 MB, xem `partitions.csv`:

| Tên      | Loại          | Offset   | Kích thước                   |
| -------- | ------------- | -------- | ---------------------------- |
| nvs      | data/nvs      | 0x9000   | 64 KB                        |
| otadata  | data/ota      | 0x19000  | 8 KB                         |
| phy_init | data/phy      | 0x1B000  | 4 KB                         |
| nvs_keys | data/nvs_keys | 0x1C000  | 4 KB                         |
| ota_0    | app           | 0x20000  | 3 MiB                        |
| ota_1    | app           | 0x320000 | 3 MiB                        |
| storage  | data/littlefs | 0x620000 | ~8,06 MiB                    |
| voice    | data/0x40     | 0xE30000 | 1,75 MB (dữ liệu giọng Pico) |
| coredump | data/coredump | 0xFF0000 | 64 KB                        |

Mỗi slot ứng dụng 3 MiB, nên file ứng dụng `.bin` phải nhỏ hơn 3 MiB (hiện khoảng 1,5 MB).

## Yêu cầu build

- **ESP-IDF 6.1** (target `esp32s3`).
- Thư viện ngoài (LVGL, esp_lvgl_port, esp_lcd_ili9341, esp_lcd_touch_ft5x06, button, cJSON, littlefs) được **ESP Component Manager** tự tải vào `managed_components/` theo `idf_component.yml` và `dependencies.lock`.
- Python của ESP-IDF (dùng cho `tools/mk_voice.py`, tự chạy khi build).

## Cấu hình

`sdkconfig` được sinh từ `sdkconfig.defaults*` và **không commit** (đã có trong `.gitignore`).

Firmware có chứa địa chỉ máy chủ:

- URL production (`CONFIG_FD_OTA_BASE_URL`, HTTPS, không kèm `/api`; `CONFIG_FD_PAIR_WEB_URL`; `CONFIG_FD_API_BASE_URL`) đặt trong `idf.py menuconfig` hoặc `sdkconfig` (file này nằm trong `.gitignore`, không commit). Trong Kconfig, mặc định là placeholder `https://your-server.example`. Khi build CI (push nhánh `prod`), script phủ overlay tạm từ `DICT_BASE` (`DICT_API_URL`) để gán URL production.
- URL Lab (`CONFIG_FD_OTA_LAB_BASE_URL`, mặc định `https://fdict-dev.fkit.vn`; `CONFIG_FD_PAIR_LAB_WEB_URL`) nằm trong `components/fd_ota/Kconfig`.

_Chế độ nhà phát triển_ (trong Cài đặt, mặc định tắt) có thể trỏ thiết bị tới Lab, nhưng **quy trình dev hiện tại không dùng OTA**. Trên nhánh dev, firmware chỉ build và nạp qua cáp USB (`idf.py build`, `idf.py -p <PORT> flash monitor`, hoặc `tools\flash-monitor.ps1`). Không phát bản OTA cho Lab. Xem [Quy tắc build firmware: dev và prod](#quy-tắc-build-firmware-dev-và-prod).

Không commit token, mật khẩu Wi-Fi hay `sdkconfig` chứa thông tin riêng. Hợp đồng API phía thiết bị: `.cursor/docs/study`, `.cursor/docs/decks`, `.cursor/docs/tts`.

## Build và nạp

Một app cho mọi máy. Giọng Pico nằm ở phân vùng `voice` (1,75 MB), chỉ chứa một ngôn ngữ. Cờ CMake `FD_VOICE_LANG` là `en` hoặc `fr`. Giá trị ghi vào cache của thư mục build và giữ nguyên cho lần build sau nếu không truyền lại.

| Môi trường          | Cách build                                                          | File nạp                                 | OTA                            |
| ------------------- | ------------------------------------------------------------------- | ---------------------------------------- | ------------------------------ |
| Dev, máy tiếng Anh  | `idf.py -B build-en -DFD_VOICE_LANG=en build` rồi flash             | USB, giọng Anh                           | Không phát hành                |
| Dev, máy tiếng Pháp | `idf.py -B build-fr -DFD_VOICE_LANG=fr build` rồi flash             | USB, giọng Pháp                          | Không phát hành                |
| Prod                | Push nhánh `prod`. Workflow build app một lần, rồi ghép hai ảnh USB | `full.bin` (Anh) và `full-fr.bin` (Pháp) | Một `app.bin`, không đổi giọng |

`idf.py build` không kèm cờ dùng thư mục `build/` và mặc định `en`. `tools\flash-monitor.ps1` chỉ nạp đúng giọng đang có trong thư mục `build/`. Muốn nạp Pháp thì build với `-DFD_VOICE_LANG=fr` trước, hoặc dùng thư mục `build-fr` và nạp bằng `idf.py -B build-fr -p <PORT> flash`.

Lingware phải có trong repo, nếu không bản Pháp không dựng được:

- Anh: `components/fd_tts/pico/lang/en-US_ta.bin`, `en-US_lh0_sg.bin`
- Pháp: `components/fd_tts/pico/lang/fr-FR_ta.bin`, `fr-FR_nk0_sg.bin`

### Dev (cáp USB)

```sh
# nạp môi trường ESP-IDF 6.1 (export.sh / export.ps1 hoặc profile của EIM)
idf.py set-target esp32s3

# Máy tiếng Anh
idf.py -B build-en -DFD_VOICE_LANG=en build
idf.py -B build-en -p <PORT> flash monitor

# Máy tiếng Pháp (thư mục build riêng để không lẫn cache với bản Anh)
idf.py -B build-fr -DFD_VOICE_LANG=fr build
idf.py -B build-fr -p <PORT> flash monitor
```

Trên Windows, sau khi đã build đúng giọng vào `build/`, có thể nạp bằng `tools\flash-monitor.ps1 [-Port COM6] [-NoFlash]`. Nhánh dev không tạo bản OTA và không đẩy bin lên server.

Build ID (màn _Thông tin phiên bản_) = 10 ký tự hex đầu (chữ thường) của SHA256 file ELF. Hai thư mục build local có thể ra hai build ID khác nhau vì đường dẫn build nằm trong ELF. Bản prod không làm vậy: một lần `idf.py build`, một app, hai ảnh USB chỉ khác `voice.bin`.

### Prod

Không build hai lần `idf.py` để phát hành. Push nhánh `prod` chạy `.github/workflows/ota-prod.yml`:

1. `scripts/ci-ota-build.sh` build app với giọng Anh mặc định.
2. Ghép `full.bin` (Pico Anh) từ `flash_args`.
3. Đóng `voice-fr.bin` bằng `tools/mk_voice.py --lang fr`, sửa dòng voice trong `flash_args`, ghép `full-fr.bin`. App, bootloader và bảng phân vùng giữ nguyên.
4. Đăng một `app.bin` lên OTA. Tải `full.bin` rồi `full-fr.bin` (`voice=fr`) vào cùng `release_id`.
5. GitHub Release đính kèm cả hai ảnh USB và `app.bin`.

Job lỗi nếu thiếu lingware Pháp, nếu hai ảnh USB trùng sha256, hoặc nếu server không giữ được cả hai file. Web-app phải đã nhận `voice=fr` trước lần push firmware này. Nếu chưa, file Pháp ghi đè file Anh.

Trang nạp: <https://fkit.vn/fdict/firmware>. Người dùng chọn Tiếng Anh hoặc Tiếng Pháp rồi bấm Nạp firmware.

### Nạp lần đầu (ảnh đầy đủ)

Máy mới, hoặc máy đang chạy bảng phân vùng cũ (trước 1.0.0, chưa có `voice`), phải nạp **một lần qua USB** bằng ảnh đầy đủ tại offset `0x0`. Ảnh gồm bootloader, bảng phân vùng, otadata, ứng dụng và đúng một giọng:

```sh
# Tiếng Anh
esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 fdict-firmware-v<ver>-<build>-full.bin
# Tiếng Pháp
esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 fdict-firmware-v<ver>-<build>-full-fr.bin
```

Cách dễ nhất: mở <https://fkit.vn/fdict/firmware> bằng Chrome/Edge, chọn ngôn ngữ, rồi bấm _Nạp firmware_ (xem [Nạp firmware qua USB từ trình duyệt](#nạp-firmware-qua-usb-từ-trình-duyệt)).

Khi đổi bảng phân vùng, dữ liệu LittleFS cũ không được giữ. Firmware không tự format; sau khi nạp full.bin qua USB, xác nhận Khôi phục cài đặt gốc để tạo lại LittleFS (xóa NVS và dữ liệu, cần ghép lại).

Ảnh đầy đủ được tạo bằng `esptool merge-bin` theo `build/flash_args`:

```sh
esptool --chip esp32s3 merge_bin -o full.bin --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x19000 ota_data_initial.bin \
  0x20000 fdict-firmware.bin 0xe30000 voice.bin
```

`--flash_mode dio` lấy đúng theo `build/flash_args` (header ảnh ghi DIO); chip vẫn chạy QIO theo `CONFIG_ESPTOOLPY_FLASHMODE_QIO` trong `sdkconfig.defaults.esp32s3`.

### Dữ liệu giọng đọc

- Nằm trong ảnh USB tại offset `0xE30000`. `full.bin` là Pico Anh. `full-fr.bin` là Pico Pháp. GitHub Release đính kèm cả hai ảnh, không đính kèm file giọng rời.
- Header `voice.bin` ghi `en` hoặc `fr` ở byte 32–35. Bản cũ toàn số 0 được firmware 5.2.0 đọc là Anh.
- Đóng gói tay: `python tools/mk_voice.py --lang en <ta.bin> <sg.bin> voice.bin` hoặc `--lang fr`. Build đã gọi lệnh này.
- Nạp riêng phân vùng giọng, không đụng app: `esptool --chip esp32s3 -p <PORT> write_flash 0xE30000 voice.bin`.
- Thiếu hoặc hỏng dữ liệu giọng: máy vẫn chạy, Pico TTS tắt, nút loa báo "Thiếu dữ liệu giọng đọc".
- Tài khoản Pháp trên máy giọng Anh (hoặc ngược lại): máy không đọc bằng Pico.

Chỉ cập nhật ứng dụng qua USB (máy đã có bảng phân vùng mới):

```sh
esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x20000 fdict-firmware-v<ver>-<build>-app.bin
```

## app.bin và full.bin

|            | app.bin                                    | full.bin       | full-fr.bin       |
| ---------- | ------------------------------------------ | -------------- | ----------------- |
| Tên file   | `fdict-firmware-vX.Y.Z-<build_id>-app.bin` | `...-full.bin` | `...-full-fr.bin` |
| Kích thước | ~1,5 MB (phải < 3 MiB)                     | ≤ 16 MiB       | ≤ 16 MiB          |
| Giọng Pico | không chứa giọng                           | Anh            | Pháp              |
| Nạp bằng   | OTA                                        | USB tại `0x0`  | USB tại `0x0`     |

- **app.bin**: chỉ chương trình FDict One, dùng cho OTA; được ghi vào `ota_0` hoặc `ota_1` (mỗi phân vùng 3 MiB) nên phải < 3 MiB. Không đụng bootloader, `/data` (LittleFS) hay `voice`. **KHÔNG** nạp app.bin ở offset `0x0`.
- **full.bin**: ảnh gộp (merged image) nạp từ `0x0` qua USB, gồm:

| Offset     | Thành phần                                               |
| ---------- | -------------------------------------------------------- |
| `0x0`      | bootloader                                               |
| `0x8000`   | bảng phân vùng (partition table)                         |
| `0x19000`  | otadata                                                  |
| `0x20000`  | ứng dụng (`ota_0`)                                       |
| `0xE30000` | dữ liệu giọng `voice` (Pico Anh hoặc Pháp, tùy file USB) |

**Khi nào dùng cái nào:** máy mới hoặc máy đổi giọng → nạp `full.bin` hoặc `full-fr.bin` qua USB. Máy đang chạy, giữ nguyên giọng → OTA chỉ nhận `app.bin`. Prod tạo cả ba file từ một lần build app và đẩy lên máy chủ (`/api/admin/releases` cho app, `/api/admin/releases/full` cho ảnh Anh, cùng endpoint với `voice=fr` cho ảnh Pháp). File nằm trên object storage (S3/MinIO).

## Ngôn ngữ giao diện (vi/en)

Chuỗi giao diện dùng [lv_i18n](https://github.com/lvgl/lv_i18n): trong code viết `_("SETTINGS")` – khoá là tiếng Anh VIẾT*HOA_GẠCH_DƯỚI (chỉ A-Z, 0-9, `*`;
chuỗi có `%s`/`%d`dùng khoá mô tả, vd`WIFI_CONNECTING_TO`). Mỗi khoá phải có giá trị đầy đủ ở cả `i18n/vi.yml`và`i18n/en.yml`. `components/fd_i18n/lv_i18n.c/.h` là file **sinh ra** nhưng được commit,
nên build ESP-IDF không cần Node.js. Sau khi thêm/sửa chuỗi, chạy từ thư mục gốc (cần Node.js):

```sh
npx lv_i18n@0.2.1 extract -s 'components/fd_ui/*.c' -t 'i18n/*.yml'   # thêm khoá mới (giá trị ~) vào cả hai file
# điền giá trị tiếng Anh (en.yml) và tiếng Việt (vi.yml) cho mọi khoá mới, rồi:
npx lv_i18n@0.2.1 compile -t 'i18n/*.yml' -o components/fd_i18n -l en
python tools/const_i18n.py
```

Chuỗi trong mảng tĩnh (ô Trang chủ, nhãn màn Thông tin, lựa chọn Học tập/Flash card) được dịch ở chỗ dùng `_(arr[i])`,
nên `extract` báo chúng là "orphaned" – đó là bình thường, không xoá. Locale gốc khi biên dịch là `en` (dự phòng khi thiếu bản dịch); giao diện mặc định vẫn là tiếng Việt
vì NVS `fd/lang` mặc định `vi`. Chuỗi kỹ thuật không có trong yml (Firmware, Build, MAC, IP…) được `_()` trả nguyên văn. Ngôn ngữ lưu NVS `fd/lang` (`vi`|`en`), đổi ở
Cài đặt > Ngôn ngữ / Language. Font tiêu đề `fd_font_noto_32_title` là tập con: ASCII + dấu tiếng Việt của các tiêu đề
(xem `tools/gen_fonts.sh`); tiêu đề tiếng Việt mới cần thêm ký tự vào `--symbols`.

## Cập nhật OTA

OTA chỉ cập nhật **ứng dụng** (file `fdict-firmware-v<ver>-<build>-app.bin`). Không dùng `full.bin` hay `full-fr.bin`. Bảng phân vùng và giọng Pico trên máy không đổi.

1. Thiết bị đã ghép, có Wi-Fi và đã đồng bộ giờ sẽ định kỳ gọi `GET {base}/api/ota/check` với các tham số `hw`, `channel`, `fw_version`, `build_id`, `device_id`, `battery`, `rssi`.
2. Máy chủ trả JSON. Nếu `update` là `false`, thiết bị coi là bản mới nhất (`check_again_s` là chu kỳ kiểm tra lại, tuỳ chọn). Nếu `update` là `true`, cần các trường `release_id`, `fw_version`, `sha256`, `size`, `url` (HTTPS). Các trường tuỳ chọn: `build_id`, `notes_vi`, `mandatory`, `min_battery`.
3. Thiết bị **không tự so sánh số phiên bản**: máy chủ quyết định có bản mới hay không. Sau khi tải, thiết bị kiểm tra SHA256 của ảnh khớp `sha256`, và phiên bản trong ảnh (bắt đầu bằng `FDict One - `) khớp `fw_version`.
4. Thiết bị ghi vào slot OTA còn lại rồi khởi động lại. Bản mới tự kiểm tra (màn hình, bộ nhớ, Wi-Fi), lỗi thì rollback. Trạng thái được báo về `POST {base}/api/ota/report`.

Phiên bản firmware là `PROJECT_VER` trong `CMakeLists.txt` gốc (ví dụ `FDict One - 1.1.8`).

### Nạp firmware qua USB từ trình duyệt

Cắm máy vào máy tính bằng cáp USB, mở <https://fkit.vn/fdict/firmware> bằng Chrome hoặc Edge, chọn **Tiếng Anh** hoặc **Tiếng Pháp**, bấm _Nạp firmware_, chọn cổng của máy rồi làm theo hướng dẫn. Không cần cài ESP-IDF. Bản đang phát hành chưa có file Pháp thì nút Pháp không nạp được.

## Phát hành OTA qua GitHub Actions

Workflow `.github/workflows/ota-prod.yml` chạy khi **push lên nhánh `prod`**:

1. Build trong container `espressif/idf:v6.1` bằng `scripts/ci-ota-build.sh`: một app, `full.bin` (Anh) và `full-fr.bin` (Pháp).
2. Upload artifact gồm app, cả hai ảnh USB và `build-info.json`.
3. Đăng bản app lên máy chủ: `POST {DICT_API_URL}/api/admin/releases`, multipart, header `X-CI-Token`, kênh `stable`, `rollout_percent=100`.
4. Tải `full.bin`: `POST {DICT_API_URL}/api/admin/releases/full`. Rồi tải `full-fr.bin` vào cùng endpoint với field `voice=fr`. Cả hai gắn một `release_id`.
5. Tạo **GitHub Release** tag `v<semver>` với `...-full.bin`, `...-full-fr.bin`, `...-app.bin` và `SHA256SUMS.txt`. Tag đã có thì bỏ qua, không ghi đè.

Bước 3 chỉ đẩy **bản app** (< 3 MiB). Hai ảnh USB đi ở bước 4. Job dừng nếu bước 4 không xác nhận được cả hai sha256, hoặc nếu sha256 Anh bị đổi sau khi tải bản Pháp.

### Biến và secret cần tạo

Job chạy trong environment **`production`**. Vào Settings → Environments → `production` (hoặc Settings → Secrets and variables → Actions) và tạo:

| Tên                | Loại                     | Giá trị                                                                                                                                                                                    |
| ------------------ | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `DICT_API_URL`     | Variable **hoặc** Secret | URL gốc máy chủ `https://host`. Workflow tự chuẩn hoá: bỏ khoảng trắng, xuống dòng, ngoặc bao quanh; chỉ giữ `https://host[:port]`, nếu có kèm đường dẫn/query thì tự cắt và báo cảnh báo. |
| `CI_RELEASE_TOKEN` | **Secret** (bắt buộc)    | token CI của máy chủ (header `X-CI-Token`)                                                                                                                                                 |

Workflow đọc `vars.DICT_API_URL`, nếu trống thì đọc `secrets.DICT_API_URL`. Bước _Mask server URL_ (`scripts/ci-url-normalize.sh`) chuẩn hoá giá trị thành `DICT_BASE`, các bước sau dùng `DICT_BASE`. Variable không được GitHub tự che, nên workflow che giá trị ngay ở bước đầu (`::add-mask::`) và không bao giờ in ra (chỉ in độ dài). GitHub vẫn có thể hiện giá trị variable trong phần `env` của bước đầu tiên; muốn giấu hoàn toàn thì dùng Secret.

Lỗi thường gặp: `DICT_API_URL rong` nghĩa là chưa đặt, hoặc đặt ở environment khác `production`.

URL production **không commit** vào repo. Khi build CI, script sinh overlay tạm `build-ci/sdkconfig.ci` từ `DICT_API_URL` để đặt `CONFIG_FD_OTA_BASE_URL`, `CONFIG_FD_PAIR_WEB_URL` (`/device-login`) và `CONFIG_FD_API_BASE_URL` (`/api/device/v1`). Các cấu hình còn lại lấy từ `sdkconfig.defaults` và `sdkconfig.defaults.esp32s3`. URL Lab mặc định nằm trong `components/fd_ota/Kconfig`. Token và URL không bao giờ được in ra log.

### Cắt một bản mới

1. Tăng `PROJECT_VER` trong `CMakeLists.txt` gốc, ví dụ `FDict One - 1.1.9`. Mỗi bản phải có phiên bản mới.
2. Thêm mục `## [1.0.3] - YYYY-MM-DD` **ở đầu** `CHANGELOG.md`, ghi thay đổi bằng tiếng Việt. Nội dung mục này thành ghi chú bản phát hành (tối đa 500 ký tự).
3. Merge hoặc push lên `prod`.

Script dừng build nếu:

- mục đầu CHANGELOG khác `PROJECT_VER`;
- phiên bản ghi trong file bin (`esp_app_desc`) khác `PROJECT_VER`;
- build có cảnh báo;
- file app từ 3 MiB trở lên;
- thiếu lingware Pico Pháp;
- `full-fr.bin` trùng sha256 với `full.bin`.

`build_id` = 10 ký tự hex đầu (chữ thường) SHA256 file ELF, giống màn _Thông tin_.

Máy chủ trả `201` khi đăng app thành công (kèm `release_id`, dùng để gắn cả hai ảnh USB và tạo GitHub Release). Nếu trả `409` (bản `fw_version`/`build_id` đã tồn tại) mà không có `release_id`, workflow **báo lỗi** và dừng, không tải ảnh USB, không tạo GitHub Release. Muốn phát hành lại: tăng `PROJECT_VER` trong `CMakeLists.txt`, thêm mục mới ở đầu `CHANGELOG.md`, rồi push lại lên `prod`. Tóm tắt job ghi `fw_version`, `build_id`, kích thước, sha256 của app và của cả hai ảnh USB, `release_id` và ghi chú.

### Phát hành dần (canary)

Trong bước _Publish OTA_:

- đổi `rollout_percent=100` thành `1`, `10` hoặc `50`;
- hoặc thêm `-F "allow_devices=<device_id>,<device_id>"` để chỉ một số máy nhận bản.

## Cấu trúc dự án

```
main/                 app_main, thông tin hệ thống
components/
  board_fnk0104b/     chân, LCD, cảm ứng, nút, LED, I2C
  fd_display/         LVGL port, xoay màn hình, đèn nền
  fd_ui/              các màn hình, điều hướng, ngủ
  fd_audio/           ES8311 + I2S, hàng đợi phát
  fd_tts/             SVOX Pico TTS (bên thứ ba)
  fd_voice/           phát âm: cache, Pico, tải nền
  fd_net/             Wi-Fi, đồng bộ giờ
  fd_ota/             OTA, ghép thiết bị, API, cache bài học
  fd_store/           LittleFS
  fd_power/           đo pin
  fd_fonts/           font sinh bằng lv_font_conv
  fd_events/          sự kiện nội bộ
assets/               font, logo gốc
.cursor/docs/{study,decks,tts}  hợp đồng API phía thiết bị
scripts/              build CI cho OTA (ci-ota-build.sh)
.github/workflows/    GitHub Actions (ota-prod.yml)
tools/                script tạo voice.bin, font, nạp và monitor
partitions.csv        bảng phân vùng 16 MB
```

## Đóng góp

- Mở issue hoặc pull request. Mô tả rõ thay đổi và cách kiểm tra trên thiết bị.
- Build phải sạch: 0 lỗi, 0 cảnh báo, ứng dụng nhỏ hơn 3 MiB.
- Kiểm tra giao diện ở cả màn dọc và ngang.
- Không đưa URL máy chủ thật, token, mật khẩu hay `sdkconfig` vào commit.
- Ghi thay đổi vào [CHANGELOG.md](CHANGELOG.md).

## Giấy phép

Mã nguồn phát hành theo **Apache License 2.0**, xem [LICENSE](LICENSE) và [NOTICE](NOTICE).
Copyright 2026 fkit.vn / Đinh Thanh Thịnh.

Tên **FDict** (thiết bị: **FDict One**), **fkit.vn** và logo FDict/fkit.vn (`assets/images/fdict-logo.svg` và các ảnh dẫn xuất) là thương hiệu thuộc sở hữu của **fkit.vn**, **không** thuộc phạm vi giấy phép Apache-2.0. Muốn sử dụng cần được fkit.vn cho phép.

Thành phần bên thứ ba (SVOX Pico TTS, LVGL, font Noto Sans/Montserrat, biểu tượng…): xem [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

## English

**FDict One** is a handheld English-vocabulary trainer on the ESP32-S3 (Freenove FNK0104B: ILI9341 LCD, FT6336U touch, ES8311 audio codec + speaker). Features: LVGL 9 UI (portrait/landscape), flash cards with a pre-session settings screen, Practice (Luyện tập) 2x2 quizzes, Dictionary (Từ điển), Word order (Sắp xếp câu), Parts of speech (Phân loại từ), Gap fill (Điền từ), auto pronunciation, offline study (lessons cached on LittleFS, results queued and sent later), offline pronunciation via SVOX Pico TTS (voice data in a separate `voice` partition), Wi-Fi manager, QR device pairing, and HTTPS OTA updates with SHA256 check and rollback.

- **Build:** ESP-IDF 6.1. Dev English: `idf.py -B build-en -DFD_VOICE_LANG=en build` then `idf.py -B build-en -p <PORT> flash`. Dev French: the same with `-B build-fr -DFD_VOICE_LANG=fr`. Prod is a push to `prod`: one app build, then `full.bin` (English Pico) and `full-fr.bin` (French Pico).
- **Configure:** production server URLs are Kconfig options (`menuconfig` / git-ignored `sdkconfig`), placeholders in Kconfig by default, and are injected at CI from `DICT_BASE`. The Lab base URL default (`https://fdict-dev.fkit.vn`) is in `components/fd_ota/Kconfig`. Dev workflow flashes over USB only; it does not publish OTA.
- **First-time flashing:** write `...-full.bin` (English) or `...-full-fr.bin` (French) at `0x0` over USB, or pick the language at <https://fkit.vn/fdict/firmware>.
- **app.bin vs full images:** app.bin (~1.5 MB, < 3 MiB) is the application only, for OTA into `ota_0`/`ota_1`, never at `0x0`, and it does not change the voice partition. Each full image is written at `0x0` (bootloader `0x0`, partition table `0x8000`, otadata `0x19000`, app `0x20000`, voice `0xE30000`). Prod uploads one app plus both full images from that single app build.
- **OTA:** updates the app image only (`fdict-firmware-v<ver>-<build>-app.bin`). The device calls `GET {base}/api/ota/check`. The server decides whether an update exists and returns `update`, `release_id`, `fw_version`, `sha256`, `size`, `url`. The device verifies the SHA256 and the version string before switching slots.
- **License:** Apache-2.0 (`LICENSE`, `NOTICE`). The FDict (device: FDict One) / fkit.vn names and logo are trademarks of fkit.vn and are not covered by the Apache license. Third-party notices: `THIRD_PARTY_NOTICES.md`.

## Quy tắc build firmware: dev và prod

- **Dev (nhánh dev):** chỉ nạp qua cáp USB. Chọn giọng bằng `-DFD_VOICE_LANG=en` hoặc `fr`, mỗi giọng một thư mục build (`build-en`, `build-fr`). `flash-monitor.ps1` nạp giọng đang nằm trong `build/`. Không dùng OTA, không tạo bản phát hành, không đẩy bin lên server.
- **Prod (nhánh prod):** OTA và hai ảnh USB chỉ xuất hiện khi push nhánh `prod`. Workflow `.github/workflows/ota-prod.yml` dựng một app và cả `full.bin` lẫn `full-fr.bin`. Gộp dev vào prod rồi push là phát cho khách ngay. Web-app phải nhận `voice=fr` trước lần push đó.
